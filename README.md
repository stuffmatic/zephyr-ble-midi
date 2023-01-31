# zephyr-ble-midi

This is a [Zephyr](https://www.zephyrproject.org/) implementation of the [BLE-MIDI service specification](BLE-MIDI-spec.pdf), which allows for wireless transfer of [MIDI](https://en.wikipedia.org/wiki/MIDI) data over [Bluetooth low energy](https://en.wikipedia.org/wiki/Bluetooth_Low_Energy).

The code for parsing and writing BLE MIDI packets covers the entire packet encoding specification, including running status, timestamp wrapping and single and multi packet system exclusive messages. To facilitate testing and reuse in other projects, this code is completely self contained and has no external dependencies.

To minimize latency, a connection interval of 7.5 ms is requested, which is the smallest interval allowed by the Bluetooth low energy specification. Jitter can be reduced (at the expense of latency) by using the timestamps of incoming messages, but this is beyond the scope of this implementation. More info about this can be found in [this post](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358).

## Usage

TODO

## Configuration options

* `BLE_MIDI_SEND_RUNNING_STATUS` - Set to `n` to disable running status (omission of repeated channel message status bytes) in sent packets. Defaults to `y`, which minimizes the size of outgoing packets.
* `BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON` - Determines if sent note off messages should be represented as note on messages with zero velocity, which increases running status efficiency. Defaults to `y`.
* `BLE_MIDI_NRF_BATCHED_TX` - When building for nRF SOC:s, this option can be set to `y` to buffer outgoing messages and send them at the start of the next connection interval (as described [here](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358)), which lowers latency and increases throughput. If set to `n` (the default) one BLE MIDI packet is sent per MIDI message.
