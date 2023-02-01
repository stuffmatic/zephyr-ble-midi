* test invalid status bytes in sysex data
* cb for raw rx/tx packet data. good for debugging different hosts.
* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* make unit tests main return non zero on failure
* disconnect API?

# ble stuff

* 2M PHY
* LLPM

# ref

* https://devzone.nordicsemi.com/guides/short-range-guides/b/bluetooth-low-energy/posts/midi-over-bluetooth-le
* https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358
* https://github.com/BLE-MIDI/NCS-MIDI/blob/main/samples/bluetooth/peripheral_midi/src/main.c
