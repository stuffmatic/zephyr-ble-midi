work_q
  - add_msg (sent from midi_tx, sets data to send flag)
  - send_current_packet (sent from radio interrupt if data to send flag is set)
  - set MTU

# ble stuff

* 2M PHY
* LLPM
* MTU  ✅
* connection interval ✅
* enabled when subscription has started ✅
* notify with callback
* BLE_MIDI_RX_ONLY config flag to avoid radio interrupt getting called needlessly
* BLE_MIDI_OPTIMIZED_TX config flag that sends one msg per packet. useful for non-nrf platforms.

# ref

* https://devzone.nordicsemi.com/guides/short-range-guides/b/bluetooth-low-energy/posts/midi-over-bluetooth-le
* https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358
* https://github.com/BLE-MIDI/NCS-MIDI/blob/main/samples/bluetooth/peripheral_midi/src/main.c
