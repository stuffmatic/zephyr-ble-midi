#include <stdio.h>
#include <assert.h>
#include "../src/ble_midi_packet.h"

typedef struct
{
    uint8_t bytes[3];
    uint16_t timestamp;
} midi_msg_t;

static void log_buffer(uint8_t *bytes, int num_bytes)
{
    for (int i = 0; i < num_bytes; i++)
    {
        printf("%02x ", ((uint8_t *)bytes)[i]);
    }
    printf("\n");
}

void assert_midi_msg_equals(midi_msg_t *actual, midi_msg_t *expected)
{
    int equal = 1;
    for (int i = 0; i < 3; i++)
    {
        if (actual->bytes[i] != expected->bytes[i])
        {
            equal = 0;
            break;
        }
    }

    /* Check if messages are equivalent. */
    uint8_t actual_high_nibble = actual->bytes[0] >> 4;
    uint8_t actual_second_data = actual->bytes[2];
    uint8_t expected_high_nibble = expected->bytes[0] >> 4;
    uint8_t expected_second_data = expected->bytes[2];

    int equivalent = actual_high_nibble == 0x8 && expected_high_nibble == 0x9 && expected_second_data == 0;
    equivalent = equivalent || (expected_high_nibble == 0x8 && actual_high_nibble == 0x9 && actual_second_data == 0);

    if (actual->timestamp != expected->timestamp && actual->bytes[0] >= 0x80)
    {
        equal = 0;
        equivalent = 0;
    }

    printf("        %s actual %02x %02x %02x (t %d), expected %02x %02x %02x (t %d)%s\n",
           equal || equivalent ? "✅" : "❌",
           actual->bytes[0], actual->bytes[1], actual->bytes[2], actual->bytes[0] < 0x80 ? 0 : actual->timestamp,
           expected->bytes[0], expected->bytes[1], expected->bytes[2], expected->bytes[0] < 0x80 ? 0 : expected->timestamp,
           equivalent ? " (equivalent)" : "");
}

void assert_payload_equals(
    struct ble_midi_writer_t *writer,
    uint8_t *expected_payload,
    int expected_payload_size)
{
    int error = expected_payload_size != writer->tx_buf_size;

    if (!error)
    {
        for (int i = 0; i < expected_payload_size; i++)
        {
            if (expected_payload[i] != writer->tx_buf[i])
            {
                error = 1;
                break;
            }
        }
    }

    printf("    %s\n", error ? "tx payload mismatch ❌" : "tx payload check OK ✅");
    printf("        expected payload: ");
    log_buffer(expected_payload, expected_payload_size);
    printf("        actual payload:   ");
    log_buffer(writer->tx_buf, writer->tx_buf_size);
    printf("\n");
}

void assert_error_code(int actual, int expected)
{
    if (actual != expected)
    {
        printf("❌ expected error code %d, got %d\n", expected, actual);
    }
}

void assert_success(int error_code)
{
    if (error_code != BLE_MIDI_SUCCESS)
    {
        printf("❌ expected success error code, got %d\n", error_code);
    }
}

void assert_equals(int actual, int expected)
{
    if (actual != expected)
    {
        printf("❌ expected %d, got %d\n", expected, actual);
    }
}

static int num_parsed_messages = 0;
static midi_msg_t parsed_messages[100];

void midi_message_cb(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp)
{
    for (int i = 0; i < 3; i++)
    {
        parsed_messages[num_parsed_messages].bytes[i] = i < num_bytes ? bytes[i] : 0;
        parsed_messages[num_parsed_messages].timestamp = timestamp;
    }
    num_parsed_messages++;
    assert(num_parsed_messages < 100);
}

void sysex_start_cb(uint16_t timestamp)
{
    midi_msg_t *msg = &parsed_messages[num_parsed_messages];
    msg->bytes[0] = 0xf7;
    msg->bytes[1] = 0;
    msg->bytes[2] = 0;
    msg->timestamp = timestamp;
    num_parsed_messages++;
    assert(num_parsed_messages < 100);
}

void sysex_data_cb(uint8_t data_byte)
{
    parsed_messages[num_parsed_messages].bytes[0] = data_byte;
    parsed_messages[num_parsed_messages].bytes[1] = 0;
    parsed_messages[num_parsed_messages].bytes[2] = 0;
    num_parsed_messages++;
    assert(num_parsed_messages < 100);
}

void sysex_end_cb(uint16_t timestamp)
{
    midi_msg_t *msg = &parsed_messages[num_parsed_messages];
    msg->bytes[0] = 0xf0;
    msg->bytes[1] = 0;
    msg->bytes[2] = 0;
    msg->timestamp = timestamp;
    num_parsed_messages++;

    assert(num_parsed_messages < 100);
}

void roundtrip_test(const char *desc,
                    int use_running_status,
                    midi_msg_t messages[],
                    int num_messages,
                    uint8_t expected_payload[],
                    int expected_payload_size)
{
    printf("Roundtrip test '%s'\n", desc);

    /* Init tx packet with the prescribed size */
    struct ble_midi_writer_t writer;
    ble_midi_writer_init(&writer, use_running_status, 1);

    /* Add messages to tx packet */
    printf("    Adding %d input messages to tx packet\n\n", num_messages);
    for (int i = 0; i < num_messages; i++)
    {
        uint8_t status_byte = messages[i].bytes[0];
        if (status_byte == 0xf7)
        {
            assert_success(ble_midi_writer_start_sysex_msg(&writer, messages[i].timestamp));
        }
        else if (status_byte == 0xf0)
        {
            assert_success(ble_midi_writer_end_sysex_msg(&writer, messages[i].timestamp));
        }
        else if (status_byte < 0x80)
        {
            ble_midi_writer_add_sysex_data(&writer, messages[i].bytes, 1, messages[i].timestamp);
        }
        else
        {
            assert_success(ble_midi_writer_add_msg(&writer, messages[i].bytes, messages[i].timestamp));
        }
    }

    /* The tx packet payload should equal the reference payload */
    assert_payload_equals(&writer, expected_payload, expected_payload_size);

    printf("    Parsing tx payload\n\n");
    /* Parse tx payload. The resulting messages should equal the original messages.*/
    num_parsed_messages = 0;
    ble_midi_parse_packet(writer.tx_buf, writer.tx_buf_size, midi_message_cb, sysex_start_cb, sysex_data_cb, sysex_end_cb);
    int n = num_messages < num_parsed_messages ? num_messages : num_parsed_messages;
    printf("    Comparing input and parsed messages\n");
    for (int i = 0; i < n; i++)
    {
        assert_midi_msg_equals(&parsed_messages[i], &messages[i]);
    }
    printf("\n");
}

void test_running_status_with_one_rt()
{
    midi_msg_t messages[] = {
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0xf6, 0, 0}, .timestamp = 11},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 11},
    };

    uint8_t expected_payload[] = {
        0x80,                   // packet header
        0x8a, 0x90, 0x69, 0x7f, // note on w timestamp
        0x69, 0x00,             // note off, running status, no timestamp
        0x69, 0x7f,             // note on, running status, no timestamp
        0x8b, 0x69, 0x00,       // note off, running status, with timestamp
        0x69, 0x7f,             // note on, running status, no timestamp
        0x8b, 0xf6,             // rt with timestamp
        0x8b, 0x69, 0x00,       // running status with timestamp
        0x69, 0x7f              // running status, no timestamp
    };

    roundtrip_test(
        "One system common message should not cancel running status",
        1,
        messages,
        sizeof(messages) / sizeof(midi_msg_t),
        expected_payload,
        sizeof(expected_payload));
}

void test_running_status_with_two_rt()
{
    midi_msg_t messages[] = {
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0xf6, 0, 0}, .timestamp = 11}, /* System common */
        {.bytes = {0xfe, 0, 0}, .timestamp = 11}, /* System real time */
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 11},
    };

    uint8_t expected_payload[] = {
        0x80,                   /* packet header */
        0x8a, 0x90, 0x69, 0x7f, /* note on w timestamp */
        0x69, 0x00,             /* note off, running status, no timestamp */
        0x69, 0x7f,             /* note on, running status, no timestamp */
        0x8b, 0x69, 0x00,       /* note off, running status, with timestamp */
        0x69, 0x7f,             /* note on, running status, no timestamp */
        0x8b, 0xf6,             /* rt with timestamp */
        0x8b, 0xfe,             /* rt with timestamp */
        0x8b, 0x69, 0x00,       /* running status with timestamp */
        0x69, 0x7f              /* running status, no timestamp */
    };

    roundtrip_test(
        "Two consecutive real time/common messages should not cancel running status",
        1,
        messages,
        sizeof(messages) / sizeof(midi_msg_t),
        expected_payload,
        sizeof(expected_payload));
}

void test_running_status_disabled()
{
    midi_msg_t messages[] = {
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 10},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 11},
        {.bytes = {0xf6, 0, 0}, .timestamp = 11},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 11}};

    uint8_t expected_payload[] = {
        0x80,                   /* packet header */
        0x8a, 0x90, 0x69, 0x7f, /* note on w timestamp */
        0x8a, 0x80, 0x69, 0x7f, /* note off w timestamp */
        0x8a, 0x90, 0x69, 0x7f, /* note on w timestamp */
        0x8b, 0x80, 0x69, 0x7f, /* note off w timestamp */
        0x8b, 0x90, 0x69, 0x7f, /* note on w timestamp */
        0x8b, 0xf6,             /* system rt w timestamp */
        0x8b, 0x80, 0x69, 0x7f, /* note off w timestamp */
    };

    roundtrip_test(
        "Timestamp and status bytes should be added for all messages when running status is disabled",
        0,
        messages,
        sizeof(messages) / sizeof(midi_msg_t),
        expected_payload,
        sizeof(expected_payload));
}

void test_full_packet()
{
    printf("Message that doesn't fit in tx buffer should not be added\n");
    midi_msg_t messages[] = {
        {.bytes = {0xb0, 0x12, 0x34}, .timestamp = 10},
        {.bytes = {0xe0, 0x12, 0x34}, .timestamp = 10},
        {.bytes = {0xb0, 0x12, 0x34}, .timestamp = 10},
        {.bytes = {0xe0, 0x12, 0x34}, .timestamp = 10},
        {.bytes = {0xb0, 0x12, 0x34}, .timestamp = 10},
        {.bytes = {0xe0, 0x12, 0x34}, .timestamp = 10}}; /* <- should not fit in the packet */

    uint8_t expected_payload[] = {
        0x80, // packet header
        0x8a, 0xb0, 0x12, 0x34,
        0x8a, 0xe0, 0x12, 0x34,
        0x8a, 0xb0, 0x12, 0x34,
        0x8a, 0xe0, 0x12, 0x34,
        0x8a, 0xb0, 0x12, 0x34,
        /* 0x8a, 0xe0, 0x12, 0x34 <- should not fit in the packet */
    };

    struct ble_midi_writer_t writer;
    ble_midi_writer_init(&writer, 1, 1);
    writer.tx_buf_max_size = 22;

    for (int i = 0; i < sizeof(messages) / sizeof(midi_msg_t); i++)
    {
        ble_midi_writer_add_msg(&writer, messages[i].bytes, messages[i].timestamp);
    }

    assert_payload_equals(&writer, expected_payload, sizeof(expected_payload));
}

void test_sysex_cancels_running_status()
{
    midi_msg_t messages[] = {
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 100},
        {.bytes = {0x80, 0x69, 0x7f}, .timestamp = 100},
        {.bytes = {0xf7, 0, 0}, .timestamp = 100},
        {.bytes = {0x01, 0, 0}, .timestamp = 100},
        {.bytes = {0x02, 0, 0}, .timestamp = 100},
        {.bytes = {0x03, 0, 0}, .timestamp = 100},
        {.bytes = {0xf0, 0, 0}, .timestamp = 100},
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 100}};

    uint8_t expected_payload[] = {
        0x81,                   // packet header
        0xe4, 0x90, 0x69, 0x7f, // note on
        0x69, 0x00,             // note off, running status
        0xe4, 0xf7,             // sysex start
        0x01, 0x02, 0x03,       // sysex data
        0xe4, 0xf0,             // sysex end
        0xe4, 0x90, 0x69, 0x7f  // note on
    };
    roundtrip_test("Sysex should cancel running status", 1, messages, sizeof(messages) / sizeof(midi_msg_t), expected_payload, sizeof(expected_payload));
}

void test_rt_in_sysex()
{
    midi_msg_t messages[] = {
        {.bytes = {0xb0, 0x12, 0x23}, .timestamp = 200},
        {.bytes = {0xf7, 0x0, 0x0}, .timestamp = 210},   // sysex start
        {.bytes = {0x01, 0, 0}, .timestamp = 220},       // sysex data
        {.bytes = {0x02, 0, 0}, .timestamp = 220},       // sysex data
        {.bytes = {0x03, 0, 0}, .timestamp = 220},       // sysex data
        {.bytes = {0xfe, 0x0, 0x0}, .timestamp = 230},   // real time
        {.bytes = {0xf0, 0x0, 0x0}, .timestamp = 240},   // sysex end
        {.bytes = {0x90, 0x69, 0x7f}, .timestamp = 250}, // note on
    };

    uint8_t expected_payload[] = {
        0x83,                   // packet header
        0xc8, 0xb0, 0x12, 0x23, // control change
        0xd2, 0xf7,             // sysex start
        0x01, 0x02, 0x03,       // sysex data
        0xe6, 0xfe,             // system rt
        0xf0, 0xf0,             // sysex end
        0xfa, 0x90, 0x69, 0x7f  // note on
    };

    roundtrip_test(
        "Real time message should be allowed in sysex message",
        1,
        messages,
        sizeof(messages) / sizeof(midi_msg_t),
        expected_payload,
        sizeof(expected_payload));
}

void test_multi_packet_sysex()
{
    printf("Sysex data should continue between packets\n");
    uint8_t sysex_data[10];
    for (int i = 0; i < sizeof(sysex_data); i++)
    {
        sysex_data[i] = i;
    }
    struct ble_midi_writer_t writer;
    ble_midi_writer_init(&writer, 1, 1);
    writer.tx_buf_max_size = 9;

    assert_success(ble_midi_writer_start_sysex_msg(&writer, 100));
    int num_bytes_added = ble_midi_writer_add_sysex_data(&writer, sysex_data, sizeof(sysex_data), 200);
    assert_equals(num_bytes_added, 6);
    uint8_t expected_payload_1[] = {0x81, 0xe4, 0xf7, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    assert_payload_equals(&writer, expected_payload_1, sizeof(expected_payload_1));

    ble_midi_writer_reset(&writer);
    num_bytes_added = ble_midi_writer_add_sysex_data(&writer, &sysex_data[num_bytes_added], sizeof(sysex_data) - num_bytes_added, 200);
    assert_equals(num_bytes_added, 4);
    assert_success(ble_midi_writer_end_sysex_msg(&writer, 100));
    uint8_t expected_payload_2[] = {0x83, 0x06, 0x07, 0x08, 0x09, 0xe4, 0xf0};
    assert_payload_equals(&writer, expected_payload_2, sizeof(expected_payload_2));
}

void test_packet_end_cancels_running_status()
{
    printf("Packet end should cancel running status\n");
    uint8_t note_on[] = {0x90, 0x69, 0x7f};
    uint8_t note_off[] = {0x80, 0x69, 0x7f};
    struct ble_midi_writer_t writer;
    ble_midi_writer_init(&writer, 1, 1);
    writer.tx_buf_max_size = 8;
    assert_success(ble_midi_writer_add_msg(&writer, note_on, 100));
    assert_success(ble_midi_writer_add_msg(&writer, note_off, 100));
    assert_error_code(ble_midi_writer_add_msg(&writer, note_on, 100), BLE_MIDI_ERROR_PACKET_FULL);

    uint8_t expected_payload_1[] = {
        0x81, 0xe4, 0x90, 0x69, 0x7f, 0x69, 0x00};
    assert_payload_equals(&writer, expected_payload_1, sizeof(expected_payload_1));

    ble_midi_writer_reset(&writer);
    assert_success(ble_midi_writer_add_msg(&writer, note_on, 100));
    assert_success(ble_midi_writer_add_msg(&writer, note_off, 100));
    uint8_t expected_payload_2[] = {
        0x81, 0xe4, 0x90, 0x69, 0x7f, 0x69, 0x00};
    assert_payload_equals(&writer, expected_payload_2, sizeof(expected_payload_2));
}

void test_disable_note_off_as_note_on()
{
    printf("Note off messages should be preserved if corresponding flag is not set\n");
    uint8_t note_on[] = {0x90, 0x69, 0x7f};
    uint8_t note_off[] = {0x80, 0x69, 0x7f};
    struct ble_midi_writer_t writer;
    ble_midi_writer_init(&writer, 1, 0);
    writer.tx_buf_max_size = 20;
    assert_success(ble_midi_writer_add_msg(&writer, note_on, 100));
    assert_success(ble_midi_writer_add_msg(&writer, note_off, 100));
    assert_success(ble_midi_writer_add_msg(&writer, note_on, 101));
    assert_success(ble_midi_writer_add_msg(&writer, note_off, 101));
    uint8_t expected_payload[] = {
        0x81,
        0xe4, 0x90, 0x69, 0x7f,
        0xe4, 0x80, 0x69, 0x7f,
        0xe5, 0x90, 0x69, 0x7f,
        0xe5, 0x80, 0x69, 0x7f};
    assert_payload_equals(&writer, expected_payload, sizeof(expected_payload));
}

void test_sysex_continuation()
{
    printf("Valid sysex continuation packets should be recognized as such\n");
    struct ble_midi_writer_t writer;
    ble_midi_writer_init(&writer, 1, 1);

    uint8_t payload_1[] = {
        0x83,                  // packet header
        0x01, 0x02, 0x03,      // sysex data
        0xe6, 0xfe,            // system rt
        0xf0, 0xf0,            // sysex end
        0xfa, 0x90, 0x69, 0x7f // note on
    };
    num_parsed_messages = 0;
    ble_midi_parse_packet(payload_1, sizeof(payload_1), midi_message_cb, sysex_start_cb, sysex_data_cb, sysex_end_cb);
    assert_equals(parsed_messages[0].bytes[0], 0x01);
    assert_equals(parsed_messages[1].bytes[0], 0x02);

    uint8_t payload_2[] = {
        0x83,                  // packet header
        0xe6, 0xfe,            // system rt
        0x01, 0x02, 0x03,      // sysex data
        0xf0, 0xf0,            // sysex end
        0xfa, 0x90, 0x69, 0x7f // note on
    };
    num_parsed_messages = 0;
    ble_midi_parse_packet(payload_2, sizeof(payload_2), midi_message_cb, sysex_start_cb, sysex_data_cb, sysex_end_cb);
    assert_equals(parsed_messages[0].bytes[0], 0xfe);
    assert_equals(parsed_messages[1].bytes[0], 0x01);
}

int main(int argc, char *argv[])
{
    test_running_status_disabled();
    test_running_status_with_one_rt();
    test_running_status_with_two_rt();
    test_sysex_cancels_running_status();
    test_rt_in_sysex();

    test_full_packet();
    test_packet_end_cancels_running_status();
    test_multi_packet_sysex();
    test_disable_note_off_as_note_on();
    test_sysex_continuation();
}
