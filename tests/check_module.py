#!/usr/bin/env python3
"""Guard the standalone package and the no-upstream-source-change boundary."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]

def read(name):
    return (root / name).read_text(encoding="utf-8-sig")

for name in ("CMakeLists.txt", "Kconfig", "zephyr/module.yml", "README.md",
             "dts/ble-mouse/receiver.dtsi", "dts/bindings/zmk,ble-mouse-receiver.yaml",
             "scripts/verify_build.py"):
    assert (root / name).is_file(), name
metadata = read("zephyr/module.yml")
assert "name: zmk-ble-mouse" in metadata
assert "cmake: ." in metadata and "kconfig: Kconfig" in metadata and "dts_root: ." in metadata
assert not (root / "compat").exists()
# Inspect module sources, excluding local build outputs and toolchains.
assert not list(root.glob("*.patch"))
for directory in (".github", "dts", "include", "scripts", "src", "tests", "zephyr"):
    assert not list((root / directory).rglob("*.patch")), directory
cmake = read("CMakeLists.txt")
for forbidden in ("execute_process", "add_custom_command", "FetchContent", "ExternalProject",
                  "APPLICATION_SOURCE_DIR", "ZEPHYR_BASE", "TARGET_OBJECTS"):
    assert forbidden not in cmake, forbidden
wrapped_symbol = "zmk_split_transport_central_peripheral_event_handler"
assert re.findall(r"--wrap=([a-z_]+)", cmake) == [wrapped_symbol]
assert cmake.count("--wrap") == 1
assert "zephyr_include_directories(include)" in cmake
for source in re.findall(r"src/[a-z_]+\.c", cmake):
    assert (root / source).is_file(), source
assert not re.search(r"^config (BT_MAX_CONN|BT_MAX_PAIRED|ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS)$",
                     read("Kconfig"), re.M)
for directory in ("src", "include", "dts", "scripts"):
    for path in (root / directory).rglob("*"):
        if path.is_file() and "__pycache__" not in path.parts:
            content = path.read_text(encoding="utf-8-sig")
            if path == root / "src/split_battery_guard.c":
                assert re.findall(r"\b__wrap_([a-z_]+)", content) == [wrapped_symbol]
                assert set(re.findall(r"\b__real_([a-z_]+)", content)) == {wrapped_symbol}
            elif directory != "scripts":
                assert "__wrap_" not in content and "__real_" not in content, path
            assert not re.search(r"\b[0-9a-f]{40}\b", content), (path, "hardcoded upstream commit")
output = read("src/mouse_output.c")
assert "<zephyr/input/input.h>" in output
assert "zmk,input-listener" in read("dts/ble-mouse/receiver.dtsi")
for forbidden in ("zmk_hid_", "zmk_hog_", "zmk_endpoints_send", "zmk_mouse_note_activity"):
    assert forbidden not in output, forbidden
print("Standalone module / no upstream patches or source replacement: passed")
