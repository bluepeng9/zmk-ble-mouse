# This changes compiler input, never upstream files or the generated .config.
if(NOT KERNEL_VERSION_MAJOR EQUAL 3 OR NOT KERNEL_VERSION_MINOR EQUAL 5)
  message(FATAL_ERROR "BLE mouse Legacy compatibility requires review for this Zephyr version (expected 3.5)")
endif()
if(NOT CONFIG_ZMK_BLE_MOUSE_CENTRAL OR NOT CONFIG_BT_SMP_SC_PAIR_ONLY OR
   NOT CONFIG_BT_SMP_APP_PAIRING_ACCEPT OR NOT CONFIG_BT_SETTINGS)
  message(FATAL_ERROR "BLE mouse Legacy compatibility requires an SC-only receiver with SMP pairing callbacks and BT settings")
endif()
if(CONFIG_BT_SMP_SC_ONLY OR CONFIG_BT_SMP_OOB_LEGACY_PAIR_ONLY OR
   CONFIG_BT_KEYS_OVERWRITE_OLDEST OR CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE OR
   CONFIG_BT_BREDR OR CONFIG_LTO)
  message(FATAL_ERROR "Unsupported security/key-layout/LTO configuration for BLE mouse Legacy compatibility")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT TARGET subsys__bluetooth__host)
  message(FATAL_ERROR "BLE mouse Legacy compatibility requires GCC and the Zephyr Bluetooth host target")
endif()

# Every translation unit must see the same bt_keys layout, including settings.
# GCC processes -include after Zephyr's -imacros autoconf.h.
set(mouse_legacy_header "${CMAKE_CURRENT_LIST_DIR}/../include/ble_mouse/legacy_config.h")
get_filename_component(mouse_legacy_header "${mouse_legacy_header}" ABSOLUTE)
zephyr_compile_options("SHELL:-include \"${mouse_legacy_header}\"")

# Read the selected host's private declarations; do not copy implementations or
# discover a parent checkout. Compile-time layout assertions reject ABI changes.
get_target_property(mouse_host_dir subsys__bluetooth__host SOURCE_DIR)
set_source_files_properties(
  "${CMAKE_CURRENT_LIST_DIR}/../src/bond_compat.c"
  "${CMAKE_CURRENT_LIST_DIR}/../src/smp_guard.c"
  TARGET_DIRECTORY app
  PROPERTIES INCLUDE_DIRECTORIES "${mouse_host_dir}")
target_sources(app PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../src/auth_guard.c"
  "${CMAKE_CURRENT_LIST_DIR}/../src/bond_compat.c"
  "${CMAKE_CURRENT_LIST_DIR}/../src/smp_guard.c")
zephyr_link_libraries(
  -Wl,--wrap=bt_conn_auth_cb_register
  -Wl,--wrap=bt_conn_auth_cb_overlay
  -Wl,--wrap=bt_smp_init
  -Wl,--wrap=settings_call_set_handler
  -Wl,--wrap=settings_save_one)
# Keep the refused-overlay entry point available for final-image verification
# even when this ZMK application currently has no overlay callers.
zephyr_link_libraries(-Wl,--undefined=__wrap_bt_conn_auth_cb_overlay)
message(STATUS "BLE mouse: Legacy SMP compiled in; mouse-only auth guard and SC bond compatibility enabled (.config retains SC_PAIR_ONLY=y)")
