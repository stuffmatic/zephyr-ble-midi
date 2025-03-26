* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* investigate disconnect when using debug_optimizations.
* something to do with https://forums.developer.apple.com/forums/thread/713095 ?
* available callback not working when disconnecting/reconnecting?
* GPIO instrumentation for measuring event trigger timing
* adding data bytes to a full packet should result in packet full instead of 0 bytes? or no?
* remove writer->in_sysex_msg and enforce this somewhere else? or check if a packet is sysex continuation?
* _t suffix on structs and enums?
* error handling in public API functions:
  * invalid data
  * device not ready
* use logging in sample app
* fix sysex contents in demo app 
* rename writer to packet?
* disable connection event trigger timer
* #define SWI_IRQn EGU0_IRQn collides with EGU_INSTANCE EGU0

# tx queue refactor

* can't select bt_central in module conf
* remove device from macos audio midi setup made it possible to connect again 🤷‍♂️
* only retry transmission after ble buffer queue full once notify cb is  called
