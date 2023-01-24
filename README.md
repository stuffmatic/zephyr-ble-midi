# zephyr-ble-midi

This is an implementation of the Bluetooth low energy MIDI (BLE-MIDI) service for the Zephyr RTOS.

The code for parsing and serializing BLE MIDI packets is completely self contained and does not have any external dependencies. This makes it both easy to test and use in other projects and TODO easier to contribute fixes.

## Configuration options

* `BLE_MIDI_SEND_RUNNING_STATUS` - Set to `n` to not use running status in tx packets. Defaults to `y`.
* `BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON` - Determines if outgoing note off messages should be represented as note on messages with zero velocity, which increases running status efficiency. Defaults to `y`.
* `BLE_MIDI_SIMPLE_TX`