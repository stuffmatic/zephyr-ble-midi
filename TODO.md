* sysex continuation where end byte is the first byte in the last packet. handle this in parser? or treat this as sysex continuation packet?
* investigate disconnect when using debug_optimizations
* available callback not working when disconnecting/reconnecting?
* softdevice uses timer0? https://devzone.nordicsemi.com/f/nordic-q-a/38502/nrfx-timer-issue
* audio midi setup disconnect button does not cause disconnect callback to be invoked, as opposed to bluetility. TODO: use notification on/off to indicate service availability?