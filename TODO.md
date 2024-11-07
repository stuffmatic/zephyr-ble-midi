* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* investigate disconnect when using debug_optimizations.
* something to do with https://forums.developer.apple.com/forums/thread/713095 ?
* available callback not working when disconnecting/reconnecting?
* return error if trying to transmit data to non-ready device
* GPIO instrumentation for measuring event trigger timing
* ~~rename ble_midi_packet_error_t to ble_midi_packet_error_t, add ble_midi_packet_error_t with DEVICE_NOT_READY, ETC~~
* ~~audio midi setup disconnect button does not cause disconnect callback to be invoked, as opposed to bluetility. TODO: use notification on/off to indicate service availability?~~

# PUBLIC API

* `enum ble_midi_error_t ble_midi_tx_msg(uint8_t *bytes)`
    * simple tx: send single packet msg, return send result
    * buffered tx: put msg in fifo, return result

* `enum ble_midi_error_t ble_midi_tx_sysex_start()`
    * simple tx: send single packet msg, return send result
    * buffered tx: put msg in fifo, return result

* `enum ble_midi_error_t ble_midi_tx_sysex_data(uint8_t *bytes, int num_bytes)`
    * simple tx: send single packet msg, return number of bytes sent
    * buffered tx: put msg in fifo, return result

* `enum ble_midi_error_t ble_midi_tx_sysex_end()`
    * simple tx: send single packet msg, return send result
    * buffered tx: put msg in fifo, return result

* `enum ble_midi_error_t ble_midi_tx_fifo_size()` 
    * buffered tx only, get the number of bytes waiting for transmission

## Serialize midi stream for use with ring buffer

serialized msg in fifo:
    timestamp 2 bytes
    3 msg bytes OR sysex data bytes


tx_queue
    num_packets_to_send
    most_recent_timestamp
    ble_midi_writers[NUM_PACKETS]
    ringbuffer
    write_work_item

    set_callbacks (msg, sysex, fifo_read)
    read

    can_fit(mgs, timestamp)

    write_msg
    write_sysex_start
    write_sysex_data
    write_sysex_end

PROBLEM: when putting message into fifo, how to know if it fits in the target packet?
* single msgs: size in target packet may be more or less than size in fifo: (timestamp?: +2 bytes, running status?: -1 byte)
* sysex start, end: (timestamp?: +2 bytes)
* sysex data: same size in packet

Message types

* fixed size midi msg, including 0xf7 and 0xf0
* sysex data (no leading status byte)

API

tx_sysex_data(data, num_bytes) -> returns number of bytes sent

## Work items

* enqueue
* radio notif