# zephyr-ble-midi

This is a Zephyr implementation of the Bluetooth low energy MIDI (BLE-MIDI) service specification.

The code for parsing and writing BLE MIDI packets covers the entire packet encoding specification, including running status, timestamp wrapping and single and multi packet sysex. This code (and the corresponding tests) is written in C, completely self contained and has no external dependencies, which means can be used in other projects as is, for example a MIDI central <- TODO

## Configuration options

* `BLE_MIDI_SEND_RUNNING_STATUS` - Set to `n` to disable running status in sent packets. Defaults to `y`.
* `BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON` - Determines if sent note off messages should be represented as note on messages with zero velocity, which increases running status efficiency. Defaults to `y`. TODO: Only applies if BLE_MIDI_SEND_RUNNING_STATUS
* `BLE_MIDI_SIMPLE_TX`

## Sysex data

Hooks providing sysex data bytes. Up to the caller to convert from 8 bit to 7 bit data and vice versa.

Sending big chunks supported through XXX callback.

## Latency and jitter

Requests a minimum connection interval of 7.5 ms. Jitter can be further reduced but beyond the scope of this implementation.

## Tests

`tests` folder contains unit tests bla.