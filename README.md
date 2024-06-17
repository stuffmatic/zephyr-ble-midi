# zephyr-ble-midi

This is a [Zephyr](https://www.zephyrproject.org/) implementation of the [BLE-MIDI service specification](BLE-MIDI-spec.pdf), which allows for wireless transfer of [MIDI](https://en.wikipedia.org/wiki/MIDI) data over [Bluetooth low energy](https://en.wikipedia.org/wiki/Bluetooth_Low_Energy). The implementation covers the entire BLE MIDI packet encoding specification, including running status, timestamp wrapping and single and multi packet system exclusive messages.

To minimize latency, a connection interval of 7.5 ms is requested, which is the smallest interval allowed by the Bluetooth low energy specification. Note that a central may or may not accept this value. 

By default, outgoing MIDI messages are sent in separate BLE packets. When building for nRF SoCs with nRF Connect SDK, outgoing non-sysex messages can optionally be buffered and transmitted in a single BLE packet just before the next connection event to reduce latency, see `CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT`. 

## Usage

The BLE MIDI service implementation is contained in a Zephyr module. The sample app's [CMakeLists.txt](CMakeLists.txt) file shows one way of adding this module to an app.

The public API is defined in [ble_midi.h](ble_midi/include/ble_midi/ble_midi.h).

## Sample app

The [sample app](src/main.c) shows how to send and receive MIDI data. The app requires a board with at least four buttons and three LEDs, for example [nrf52840dk_nrf52840](https://docs.zephyrproject.org/latest/boards/arm/nrf52840dk_nrf52840/doc/index.html).

* __Button 1__ - Send three simultaneous note on/off messages
* __Button 2__ - Send a short sysex message
* __Button 3__ - Send a long, streaming sysex message
* __Button 4__ - Send enqueued messages (only if `CONFIG_BLE_MIDI_TX_MODE_MANUAL` is set)
* __LED 1__ - On when connected to a BLE MIDI central
* __LED 2__ - Toggles on/off when receiving sysex messages
* __LED 3__ - Toggles on/off when receiving non-sysex messages

## Configuration options

* `CONFIG_BLE_MIDI_SEND_RUNNING_STATUS` - Set to `y` to enable running status (omission of repeated channel message status bytes) in transmitted packets. Defaults to `n`.
* `CONFIG_BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON` - Determines if transmitted note off messages should be represented as note on messages with zero velocity, which increases running status efficiency. Defaults to `n`.
* `CONFIG_BLE_MIDI_TX_PACKET_MAX_SIZE` - Determines the maximum size of transmitted BLE MIDI packets (clamped to the MTU - 3).
* Use one of the following options to control how transmission of outgoing BLE packets is triggered:
  * `CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG` - Outgoing MIDI messages are sent in separate BLE packets. This is the default option. May have a negative impact on latency but does not rely on nRF Connect SDK specific APIs and should work out of the box on nRF multi core SoCs.
  * `CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT` - Buffer outgoing MIDI messages and send them in a single BLE packet just before the next connection event to reduce latency. Use with nRF Connect SDK v2.6.0 or newer. Relies on the Event Trigger API added in v2.6.0.
  * `CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT_LEGACY` - Use with nRF Connect SDK versions older than v2.6.0. The same as `CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT` but relies on the MPSL radio notifications API that was removed in v2.6.0.
  * `CONFIG_BLE_MIDI_TX_MODE_MANUAL` - Buffer outgoing MIDI messages and leave it up to the caller to trigger transmission. Can be useful in combination with a custom connection event notification mechanism.

## nRF multi-core considerations

When `CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT` is enabled, it is assumed that the BLE controller runs on the same core as the BLE MIDI service. This is true for a single core SoC like the nRF52840 or when running the entire application on the network core of a multi core SoC like the nRF5340. When running the BLE controller and host on separate cores, you currently have these options:

* Send one MIDI message per BLE packet using `CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG`.
* Roll your own mechanism for relaying the appropriate notifications from the network core to the application core to achieve BLE packet transmission synchronized to the start of connection events. See `CONFIG_BLE_MIDI_TX_MODE_MANUAL`.
