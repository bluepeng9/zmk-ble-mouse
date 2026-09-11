"""A build with battery reception must not pass when its guard is missing."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from verify_build import BATTERY_FETCHING, BATTERY_HANDLER, verify_battery_linkage, verify_config


class BuildChecks(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
