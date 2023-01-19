#include "ble_midi.h"

uint32_t is_sysex_message(uint8_t status_byte)
{
    switch (status_byte)
    {
    case 0xf7: /* Sysex start */
    case 0xf0: /* Sysex end */
        return 1;
    }

    return 0;
}

uint32_t is_realtime_message(uint8_t status_byte)
{
    switch (status_byte)
    {
    case 0xf8: /* Timing Clock */
    case 0xfa: /* Start */
    case 0xfb: /* Continue */
    case 0xfc: /* Stop */
    case 0xfe: /* Active Sensing */
    case 0xff: /* System Reset */
        return 1;
    }

    return 0;
}

uint32_t is_channel_message(uint8_t status_byte)
{
    uint8_t high_nibble = status_byte >> 4;
    switch (high_nibble)
    {
    case 0x8: /* Note off */
    case 0x9: /* Note on */
    case 0xa: /* Poly KeyPress */
    case 0xb: /* Control Change */
    case 0xe: /* PitchBend Change */
    case 0xc: /* Program Change */
    case 0xd: /* Channel Pressure */
        return 1;
    }

    return 0;
}

uint8_t message_size(uint8_t status_byte)
{
    /* Channel message? */
    uint8_t high_nibble = status_byte >> 4;
    switch (high_nibble)
    {
    case 0x8: /* Note off */
    case 0x9: /* Note on */
    case 0xa: /* Poly KeyPress */
    case 0xb: /* Control Change */
    case 0xe: /* PitchBend Change */
        /* Three byte channel Voice Message */
        return 3;
    case 0xc: /* Program Change */
    case 0xd: /* Channel Pressure */
        /* Two byte channel Voice Message */
        return 2;
    default:
        break;
    }

    /* Real time message? */
    switch (status_byte)
    {
    case 0xf8: /* Timing Clock */
    case 0xfa: /* Start */
    case 0xfb: /* Continue */
    case 0xfc: /* Stop */
    case 0xfe: /* Active Sensing */
    case 0xff: /* System Reset */
        return 1;
    default:
        break;
    }

    /* System common message? */
    switch (status_byte)
    {
    case 0xf1: /* MIDI Time Code Quarter Frame */
    case 0xf3: /* Song Select */
        /* 2 byte System Common message */
        return 2;
    case 0xf2: /* Song Position Pointer */
        /* 3 byte System Common message */
        return 3;
    case 0xf6: /* Tune request */
               /* Single-byte System Common Message */
        return 1;
    }

    return 0;
}

void ble_midi_parse_packet(uint8_t *packet, uint32_t num_bytes, struct ble_midi_callbacks *callbacks)
{
    if (num_bytes < 2)
    {
        /* Empty packet */
        return;
    }

    uint8_t header = packet[0];
    if (!(header & 0x80))
    {
        /* Invalid header byte */
        return;
    }
    if (header & 0x40)
    {
        /* Warning: expected reserved byte to be zero */
    }
    uint8_t timestamp_high = header & 0x3f;

    uint8_t second_byte = packet[1];
    if (second_byte & 0x80)
    {
    }
    else
    {
        /* Sysex continuation packet */
    }

    uint8_t running_status = 0;
    uint32_t read_idx = 1;
    while (read_idx < num_bytes)
    {
    }
}

void ble_midi_out_packet_reset(struct ble_midi_out_packet *packet)
{
    packet->size = 0;
    packet->max_size = BLE_MIDI_PACKET_MAX_SIZE;
    packet->running_status = 0;
}

uint32_t ble_midi_out_packet_add_message(
    struct ble_midi_out_packet *packet,
    uint8_t *message_bytes, /* 3 bytes, zero padded */
    uint16_t timestamp)
{
    uint8_t status_byte = message_bytes[0];
    uint8_t data_bytes[2] = {message_bytes[1], message_bytes[2]};
    uint8_t num_message_bytes = message_size(status_byte);

    if (num_message_bytes == 0)
    {
        /* Unsupported/invalid status byte */
        return 1;
    }

    if (packet->size == 0)
    {
        /* Packet is empty. Create a header byte. */
        packet->bytes[0] = 0x80 | ((timestamp >> 6) & 0xf);
        packet->size += 1;
    }

    uint32_t use_running_status = 0;
    if (is_channel_message(status_byte))
    {
        if ((status_byte >> 4) == 0x8)
        {
            /* This is a note off message. Represent it as a note on with velocity 0
               to increase running status efficiency. */
            status_byte = 0x90 | (status_byte & 0xf);
            data_bytes[1] = 0;
        }
        if (status_byte == packet->running_status)
        {
            use_running_status = 1;
        }
        packet->running_status = status_byte;
    }
    else if (!is_realtime_message(status_byte))
    {
        /* From the spec:
           Running Status will be stopped when any other Status byte intervenes.
           Real-Time messages should not affect Running Status. */
        packet->running_status = 0;
    }

    uint8_t num_bytes_to_write = 1 + num_message_bytes; /* timestamp byte + message bytes */
    if (use_running_status)
    {
        num_bytes_to_write -= 1; /* skip message status byte */
    }

    if (packet->size + num_bytes_to_write > packet->max_size)
    {
        /* Can't fit message in packet */
        return 1;
    }

    /* write timestamp byte (TODO: Omit this (sometimes)? Not required by the spec for running status. ) */
    uint8_t timestamp_low = timestamp & 0x7f;
    packet->bytes[packet->size] = 0x80 | timestamp_low;
    packet->size++;

    /* write status byte? */
    if (!use_running_status)
    {
        packet->bytes[packet->size] = status_byte;
        packet->size++;
    }

    /* write data bytes */
    for (int i = 0; i < num_message_bytes - 1; i++)
    {
        packet->bytes[packet->size] = data_bytes[i];
        packet->size++;
    }

    /* Message added to packet. */
    return 0;
}