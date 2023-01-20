* running status only within packet
* support running status for tx (use note on w vel 0 as note off)
* use parser state object(s), to support multiple inputs
   * tx
      * running status
      * in_sysex_sequence
   * rx
      * running status
      * in_sysex_sequence
* hooks:
   * msg_rx(bytes, byte count, timestamp)
   * sysex_rx(bytes, byte count, start/cont/end)
   * available y/n
* tx_msg(bytes, byte count)
* tx_sysex(bytes, byte count, start/cont/end)

# ble stuff

* 2M PHY
* LLPM
* MTU
* connection interval
* enabled when subscription has started ✅
* notify with callback

# ref

* https://devzone.nordicsemi.com/guides/short-range-guides/b/bluetooth-low-energy/posts/midi-over-bluetooth-le
* https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358