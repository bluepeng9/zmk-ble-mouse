#!/usr/bin/env python3
"""Check receiver configuration, effective compiler mode and guard linkage."""
import argparse
import json
from pathlib import Path
import re

BATTERY_FETCHING = "CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING"
BATTERY_HANDLER = "zmk_split_transport_central_peripheral_event_handler"
LEGACY = "CONFIG_ZMK_BLE_MOUSE_LEGACY_PAIRING"
LEGACY_WRAPPERS = ("bt_conn_auth_cb_register", "bt_conn_auth_cb_overlay", "bt_smp_init",
                   "settings_call_set_handler", "settings_save_one")


def final_map_path(config, build):
    name = config.get("CONFIG_KERNEL_BIN_NAME", '"zephyr"').strip('"')
    return build / "zephyr" / (name + ".map")


def verify_config(config, require_enabled=False):
    if config.get("CONFIG_ZMK_BLE_MOUSE_CENTRAL") != "y":
        if require_enabled:
            raise ValueError("BLE mouse receiver was requested but is absent from the build")
        return "BLE mouse receiver disabled"

    for option in ("CONFIG_ZMK_POINTING", "CONFIG_BT_GATT_CLIENT", "CONFIG_BT_SMP", "CONFIG_SETTINGS"):
        if config.get(option) != "y":
            raise ValueError(f"BLE mouse receiver requires {option}=y")
    if config.get("CONFIG_BT_KEYS_OVERWRITE_OLDEST") == "y":
        raise ValueError("BLE mouse receiver must not evict existing bonds")
    if config.get("CONFIG_BT_FILTER_ACCEPT_LIST") == "y":
        raise ValueError("BLE mouse auto-connect requires BT_FILTER_ACCEPT_LIST disabled")
    if config.get(LEGACY) == "y":
        for option in ("CONFIG_BT_SMP_SC_PAIR_ONLY", "CONFIG_BT_SMP_APP_PAIRING_ACCEPT",
                       "CONFIG_BT_SETTINGS"):
            if config.get(option) != "y":
                raise ValueError(f"Legacy compatibility requires original {option}=y")
        for option in ("CONFIG_BT_SMP_SC_ONLY", "CONFIG_BT_SMP_OOB_LEGACY_PAIR_ONLY",
                       "CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE", "CONFIG_BT_BREDR", "CONFIG_LTO"):
            if config.get(option) == "y":
                raise ValueError(f"Unsupported Legacy compatibility setting: {option}")
    elif config.get("CONFIG_BT_SMP_SC_PAIR_ONLY") == "y":
        return "SC-only receiver: Legacy mice cannot pair without the Legacy compatibility option"
    return "BLE mouse configuration verified"


def verify_battery_linkage(config, map_path):
    if config.get("CONFIG_ZMK_BLE_MOUSE_CENTRAL") != "y" or config.get(BATTERY_FETCHING) != "y":
        return None
    try:
        link_map = map_path.read_text(encoding="utf-8")
    except OSError as error:
        raise ValueError(f"Battery fetching is enabled; its guard needs a final linker map: {map_path}") from error
    # Require address-bearing definitions, not a name in a discarded input
    # section or the cross-reference table. The host test separately exercises
    # actual call redirection; symbol presence does not prove runtime behavior.
    for symbol in (BATTERY_HANDLER, "__wrap_" + BATTERY_HANDLER):
        if not re.search(r"^\s+0x[0-9a-fA-F]+\s+" + re.escape(symbol) + r"\s*$", link_map, re.M):
            raise ValueError(f"Final linker map is missing the battery guard/original symbol: {symbol}")
    return "Split battery fetching enabled; guard and original handler present in final linker map"


def verify_legacy_build(config, build):
    if config.get("CONFIG_ZMK_BLE_MOUSE_CENTRAL") != "y" or config.get(LEGACY) != "y":
        return None
    try:
        link_map = final_map_path(config, build).read_text(encoding="utf-8")
        commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError("Legacy compatibility requires the final linker map and compile_commands.json") from error
    # The overlay is intentionally refused and does not call its original.
    symbols = ["__wrap_" + name for name in LEGACY_WRAPPERS]
    symbols += [name for name in LEGACY_WRAPPERS if name != "bt_conn_auth_cb_overlay"]
    symbols += ["zmk_ble_mouse_legacy_pairing_allowed"]
    for symbol in symbols:
        if not re.search(r"^\s+0x[0-9a-fA-F]+\s+" + re.escape(symbol) + r"\s*$", link_map, re.M):
            raise ValueError(f"Legacy policy/bond linkage missing: {symbol}")
    required = {"smp.c", "keys.c", "auth_guard.c", "bond_compat.c", "smp_guard.c"}
    for entry in commands:
        source = entry["file"].replace("\\", "/")
        if source.rsplit("/", 1)[-1] in required:
            required.remove(source.rsplit("/", 1)[-1])
        if source.lower().endswith((".c", ".cpp", ".cc", ".s")):
            command = entry.get("command", " ".join(entry.get("arguments", [])))
            if "-include" not in command or "legacy_config.h" not in command:
                raise ValueError(f"Inconsistent Legacy config: forced header missing from {source}")
    if required:
        raise ValueError(f"Legacy build is missing source commands: {', '.join(sorted(required))}")
    return "Legacy SMP included consistently; auth/SMP/bond guards linked (.config remains SC_PAIR_ONLY=y)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, help="Zephyr build directory")
    parser.add_argument("--require-enabled", action="store_true")
    args = parser.parse_args()
    config = dict(line.split("=", 1) for line in
                  (args.build / "zephyr/.config").read_text(encoding="utf-8").splitlines()
                  if line.startswith("CONFIG_") and "=" in line)
    try:
        print(verify_config(config, args.require_enabled))
        battery = verify_battery_linkage(config, final_map_path(config, args.build))
        if battery:
            print(battery)
        legacy = verify_legacy_build(config, args.build)
        if legacy:
            print(legacy)
        if config.get("CONFIG_ZMK_BLE_MOUSE_CENTRAL") == "y":
            print("Firmware runtime and hardware behavior still need validation")
    except ValueError as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
