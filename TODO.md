* test invalid status bytes in sysex data
* cb for raw rx/tx packet data. good for debugging different hosts.
* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* make unit tests main return non zero on failure
* disconnect API?
* select/dpend on BT_* config vars
* don't use softdevice?
* fix stack overflow when receiving (sysex) events. probably related to printk. use work queue/ringbuffer to process data safely in the sample app
* mention that batch tx requires softdevice
* use dedicated work queue internally?
* remove inSysexMessage from writer

# ble stuff

* 2M PHY
* LLPM

# ref

* https://devzone.nordicsemi.com/guides/short-range-guides/b/bluetooth-low-energy/posts/midi-over-bluetooth-le
* https://github.com/BLE-MIDI/NCS-MIDI/blob/main/samples/bluetooth/peripheral_midi/src/main.c
