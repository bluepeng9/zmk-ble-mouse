#!/usr/bin/env python3
"""Run client/output, parser and GNU linker battery/security/storage guard tests."""
import argparse
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="C11 compiler with a GNU --wrap capable linker, e.g. gcc")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    output = ROOT / "build" / "mouse-tests"
    output.mkdir(parents=True, exist_ok=True)
    command = shlex.split(args.cc, posix=sys.platform != "win32")
    command += ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += ["-I", str(ROOT / "include")]
    suites = {
        "test_mouse": ["src/hid_parser.c", "src/button_state.c"],
        "test_client": ["src/hid_parser.c", "tests/fake_runtime.c"],
        "test_output": ["src/button_state.c", "tests/fake_runtime.c"],
        "test_radio": ["tests/fake_runtime.c"],
        "test_battery_guard": ["src/split_battery_guard.c", "tests/fixtures/split_event_sink.c"],
        "test_auth_guard": ["src/auth_guard.c", "tests/fixtures/auth_sink.c"],
        "test_bond_compat": ["src/bond_compat.c", "tests/fixtures/settings_sink.c"],
        "test_smp_guard": ["src/smp_guard.c", "tests/fixtures/smp_sink.c"],
    }
    for name, sources in suites.items():
        executable = output / (name + (".exe" if sys.platform == "win32" else ""))
        options = [] if name == "test_mouse" else [
            "-I", str(ROOT / "tests/stubs"), "-Wno-unused-parameter"]
        if name == "test_radio":
            options += ["-DFAKE_REAL_RADIO=1"]
        if name == "test_battery_guard":
            options += ["-Wl,--wrap=zmk_split_transport_central_peripheral_event_handler"]
        if name == "test_auth_guard":
            options += ["-Wl,--wrap=bt_conn_auth_cb_register", "-Wl,--wrap=bt_conn_auth_cb_overlay"]
        if name == "test_bond_compat":
            options += ["-DZMK_BLE_MOUSE_LEGACY_CONFIG_APPLIED=1",
                        "-Wl,--wrap=settings_call_set_handler", "-Wl,--wrap=settings_save_one"]
        if name == "test_smp_guard":
            options += ["-Wl,--wrap=bt_smp_init"]
        compile_command = command + options + [str(ROOT / s) for s in sources]
        compile_command += [str(ROOT / f"tests/{name}.c"), "-o", str(executable)]
        subprocess.run(compile_command, check=True, cwd=ROOT)
        subprocess.run([str(executable)], check=True, cwd=ROOT)
        if name == "test_bond_compat":
            # Optional signing changes the on-flash key record prefix.
            subprocess.run(compile_command + ["-DCONFIG_BT_SIGNING=1"], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)
    subprocess.run([sys.executable, str(ROOT / "tests/test_build_checks.py")], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
