# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BLE_MIDI)
  zephyr_include_directories(./include)

  zephyr_library()
  zephyr_library_sources(./src/ble_midi_packet.c ./src/ble_midi.c ./src/ble_midi_context.c ./src/tx_queue.c)
  zephyr_library_sources_ifdef(CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT ./src/conn_event_trigger.c)
  zephyr_library_sources_ifdef(CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT_LEGACY ./src/conn_event_trigger_legacy.c)
endif()
