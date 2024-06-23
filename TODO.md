* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* investigate disconnect when using debug_optimizations.
* something to do with https://forums.developer.apple.com/forums/thread/713095 ?
* available callback not working when disconnecting/reconnecting?
* return error if trying to transmit data to non-ready device
* rename ble_midi_error_t to ble_midi_packet_error_t, add ble_midi_error_t with DEVICE_NOT_READY, ETC
* audio midi setup disconnect button does not cause disconnect callback to be invoked, as opposed to bluetility. TODO: use notification on/off to indicate service availability?