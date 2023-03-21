# zephyr-ble-midi

This is a [Zephyr](https://www.zephyrproject.org/) implementation of the [BLE-MIDI service specification](BLE-MIDI-spec.pdf), which allows for wireless transfer of [MIDI](https://en.wikipedia.org/wiki/MIDI) data over [Bluetooth low energy](https://en.wikipedia.org/wiki/Bluetooth_Low_Energy).

The implementation covers the entire BLE MIDI packet encoding specification, including running status, timestamp wrapping and single and multi packet system exclusive messages.

To minimize latency, a connection interval of 7.5 ms is requested, which is the smallest interval allowed by the Bluetooth low energy specification (note that a central may or may not accept this value). Also, outgoing MIDI events may optionally be accumulated and transmitted at the start of the next connection event to reduce latency (see the `BLE_MIDI_NRF_BATCHED_TX` configuration option). This technique is described in [this post](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358), which also covers strategies for reducing jitter (at the expense of latency) that are currently beyond the scope of this implementation.

## Usage

The BLE MIDI service implementation is contained in a Zephyr module. The sample app's [CMakeLists.txt](CMakeLists.txt) file shows one way of adding this module to an app.

The public API is defined in [ble_midi.h](ble_midi/include/ble_midi/ble_midi.h).

## Sample app

The [sample app](src/main.c) shows how to send and receive MIDI data. The app requires a board with at least three buttons and three LEDs, for example [nrf52840dk_nrf52840](https://docs.zephyrproject.org/latest/boards/arm/nrf52840dk_nrf52840/doc/index.html) or [nrf5340dk_nrf5340](https://docs.zephyrproject.org/latest/boards/arm/nrf5340dk_nrf5340/doc/index.html).

* __Button 1__ - Send three simultaneous note on/off messages
* __Button 2__ - Send a short sysex message
* __Button 3__ - Send a long, streaming sysex message
* __LED 1__ - On when connected to a BLE MIDI central
* __LED 2__ - Toggles on/off when receiving sysex messages
* __LED 3__ - Toggles on/off when receiving non-sysex messages

## Configuration options

* `CONFIG_BLE_MIDI_SEND_RUNNING_STATUS` - Set to `n` to disable running status (omission of repeated channel message status bytes) in sent packets. Defaults to `y`, which minimizes the size of outgoing packets.
* `CONFIG_BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON` - Determines if sent note off messages should be represented as note on messages with zero velocity, which increases running status efficiency. Defaults to `y`.
* `CONFIG_BLE_MIDI_NRF_BATCHED_TX` - When building for nRF SOC:s, this option can be set to `y` to buffer outgoing messages and send them at the start of the next connection interval (as described [here](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358)), which reduces latency and increases throughput. If set to `n` (the default) one BLE MIDI packet is sent per MIDI message.
* `CONFIG_BLE_MIDI_TX_PACKET_MAX_SIZE` - Determines the maximum size of transmitted BLE MIDI packets. 