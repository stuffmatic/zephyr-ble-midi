* test invalid status bytes in sysex data
* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* make unit tests main return non zero on failure, clean up assertions etc
* disconnect API?
* fix stack overflow when receiving (sysex) events. probably related to printk. use work queue/ringbuffer to process data safely in the sample app
* mention that batch tx requires softdevice (does it?)
* use dedicated work queue internally?

# ble stuff

* 2M PHY
* LLPM