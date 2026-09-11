"""A build with battery reception must not pass when its guard is missing."""
from pathlib import Path
import json
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_build import (BATTERY_FETCHING, BATTERY_HANDLER, LEGACY, LEGACY_WRAPPERS,
                          final_map_path, verify_battery_linkage, verify_config, verify_legacy_build)


class BuildChecks(unittest.TestCase):
    def test_kernel_output_name_selects_the_final_map(self):
        build = Path("build")
        self.assertEqual(final_map_path({}, build), build / "zephyr/zephyr.map")
        self.assertEqual(final_map_path({"CONFIG_KERNEL_BIN_NAME": '"zmk"'}, build),
                         build / "zephyr/zmk.map")

    def test_battery_enabled_build_requires_both_definitions(self):
        config = {name: "y" for name in (
            "CONFIG_ZMK_BLE_MOUSE_CENTRAL", "CONFIG_ZMK_POINTING", "CONFIG_BT_GATT_CLIENT",
            "CONFIG_BT_SMP", "CONFIG_SETTINGS", BATTERY_FETCHING)}
        verify_config(config, require_enabled=True)
        symbols = (BATTERY_HANDLER, "__wrap_" + BATTERY_HANDLER)
        with TemporaryDirectory() as directory:
            link_map = Path(directory) / "zephyr.map"
            with self.assertRaises(ValueError):
                verify_battery_linkage(config, link_map)
            definitions = [f"                0x0000000000012000 {symbol}\n" for symbol in symbols]
            link_map.write_text("".join(definitions), encoding="utf-8")
            self.assertIsNotNone(verify_battery_linkage(config, link_map))
            for missing in range(2):
                # A discarded section or cross-reference mention must not hide
                # a missing definition of either the guard or original handler.
                link_map.write_text(definitions[1 - missing] +
                                    f" .text.{symbols[missing]} 0x0 0x40 libapp.a\n" +
                                    f"{symbols[missing]} libapp.a\n", encoding="utf-8")
                with self.assertRaises(ValueError):
                    verify_battery_linkage(config, link_map)

    def test_non_receiver_build_does_not_require_mouse_linkage(self):
        self.assertIsNone(verify_battery_linkage({BATTERY_FETCHING: "y"}, Path("absent.map")))
        with self.assertRaises(ValueError):
            verify_config({}, require_enabled=True)

    def test_legacy_build_must_have_all_guards_and_consistent_compiler_config(self):
        config = {"CONFIG_ZMK_BLE_MOUSE_CENTRAL": "y", LEGACY: "y"}
        symbols = ["__wrap_" + s for s in LEGACY_WRAPPERS]
        symbols += [s for s in LEGACY_WRAPPERS if s != "bt_conn_auth_cb_overlay"]
        symbols += ["zmk_ble_mouse_legacy_pairing_allowed"]
        commands = [{"file": "host/" + name,
                     "command": "gcc -imacros autoconf.h -include legacy_config.h -c " + name}
                    for name in ("smp.c", "keys.c", "auth_guard.c", "bond_compat.c", "smp_guard.c", "conn.c")]
        with TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "zephyr").mkdir()
            with self.assertRaises(ValueError):
                verify_legacy_build(config, build)
            definitions = [f"  0x00001000 {s}\n" for s in symbols]
            link_map = build / "zephyr/zephyr.map"
            link_map.write_text("".join(definitions), encoding="utf-8")
            database = build / "compile_commands.json"
            database.write_text(json.dumps(commands), encoding="utf-8")
            self.assertIsNotNone(verify_legacy_build(config, build))
            for index in range(len(definitions)):
                link_map.write_text("".join(d for i, d in enumerate(definitions) if i != index), encoding="utf-8")
                with self.assertRaises(ValueError):
                    verify_legacy_build(config, build)
            link_map.write_text("".join(definitions), encoding="utf-8")
            for index in range(len(commands)):
                broken = [dict(c) for c in commands]
                broken[index]["command"] = "gcc -c source.c"
                database.write_text(json.dumps(broken), encoding="utf-8")
                with self.assertRaises(ValueError):
                    verify_legacy_build(config, build)

    def test_legacy_policy_rejects_conflicting_configs(self):
        config = {name: "y" for name in (
            "CONFIG_ZMK_BLE_MOUSE_CENTRAL", "CONFIG_ZMK_POINTING", "CONFIG_BT_GATT_CLIENT",
            "CONFIG_BT_SMP", "CONFIG_SETTINGS", LEGACY, "CONFIG_BT_SETTINGS",
            "CONFIG_BT_SMP_SC_PAIR_ONLY", "CONFIG_BT_SMP_APP_PAIRING_ACCEPT")}
        verify_config(config)
        for bad in ("CONFIG_LTO", "CONFIG_BT_BREDR", "CONFIG_BT_SMP_SC_ONLY",
                    "CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE"):
            with self.assertRaises(ValueError):
                verify_config(dict(config, **{bad: "y"}))


if __name__ == "__main__":
    unittest.main()
