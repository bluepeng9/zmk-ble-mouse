#!/usr/bin/env python3
"""Check receiver configuration and battery guard linkage without a version pin."""
import argparse
from pathlib import Path
import re

BATTERY_FETCHING = "CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING"
BATTERY_HANDLER = "zmk_split_transport_central_peripheral_event_handler"


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
        battery = verify_battery_linkage(config, args.build / "zephyr/zephyr.map")
        if battery:
            print(battery)
        if config.get("CONFIG_ZMK_BLE_MOUSE_CENTRAL") == "y":
            print("Firmware runtime and hardware behavior still need validation")
    except ValueError as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
