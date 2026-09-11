zephyr_include_directories(include)

# ZMK exposes its headers privately on app. Use that target so this module
# needs no hardcoded path to a ZMK checkout or copies of upstream headers.
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_BLE_MOUSE app PRIVATE src/behavior_ble_mouse.c)
target_sources_ifdef(CONFIG_ZMK_BLE_MOUSE_CENTRAL app PRIVATE
  src/hid_parser.c src/button_state.c src/central_scan.c src/ble_mouse.c src/mouse_output.c)

if(CONFIG_ZMK_BLE_MOUSE_CENTRAL AND CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
  target_sources(app PRIVATE src/split_battery_guard.c)
  zephyr_link_libraries(-Wl,--wrap=zmk_split_transport_central_peripheral_event_handler)
endif()

if(CONFIG_ZMK_BLE_MOUSE_LEGACY_PAIRING)
  include(cmake/legacy_pairing.cmake)
endif()
