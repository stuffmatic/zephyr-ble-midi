# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BLE_MIDI)
  zephyr_include_directories(./include)

  zephyr_library()
  zephyr_library_sources(
    ./src/ble_midi_packet.c ./src/ble_midi.c ./src/ble_midi_context.c
    )
endif()
