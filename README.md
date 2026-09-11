# zmk-ble-mouse

An independent Zephyr module for ZMK that receives BLE HID reports from one
Logitech M720. The module
contains its own parser, GATT client, input device, behavior and host tests. It
uses Zephyr/ZMK APIs without modifying, patching or replacing upstream source
files. Module-owned linker wrappers validate split battery events and, when
Legacy compatibility is enabled, enforce mouse pairing policy and preserve
SC-only bond storage. A compiler header includes Legacy SMP support in the
receiver image; this explicitly changes the compiled Bluetooth host policy.

**Status:** Legacy compatibility addresses the SC-only build configuration found
during M720 pairing diagnosis. The nice_nano_v2 central dongle image compiles and
links with the compatibility layer. Physical M720 pairing/report validation
remains outstanding; host tests and a build do not establish hardware readiness.
Split keyboard battery fetching remains enabled with its invalid-source guard.

## Data path and package boundary

```text
M720 BLE HID -> module GATT client -> module HID parser
             -> Zephyr input device -> ZMK input listener
             -> keyboard's selected USB/BLE OUT
```

The parser supports relative X/Y, five buttons (including back/forward), wheel
and horizontal pan. It uses HID Report Maps and GATT Report References rather
than a fixed boot-mouse format. Unifying, HID++ and Logi Options+ are outside its
scope. Report fixtures are synthetic; physical M720 reports have not been captured.

| Location | Responsibility |
| --- | --- |
| `src/hid_parser.c`, `src/button_state.c` | Portable decoding and button state |
| `src/ble_mouse.c`, `src/central_scan.c` | Pairing, bonds, GATT and radio requests |
| `src/mouse_output.c` | Virtual Zephyr input device and endpoint event listener |
| `src/behavior_ble_mouse.c` | Pair/Clear behavior |
| `src/split_battery_guard.c` | Bounds check before the original split battery handler |
| `cmake/legacy_pairing.cmake`, `include/ble_mouse/legacy_config.h` | Consistent receiver-wide Legacy compiler configuration |
| `src/auth_guard.c`, `src/smp_guard.c` | Mouse-only Legacy admission and local SC capability requirement |
| `src/bond_compat.c` | Read old SC bond records and retain their original storage format |
| `include/`, `dts/` | Module interfaces and devicetree bindings |
| `scripts/verify_build.py` | Receiver configuration and compatibility checks |
| `tests/` | Parser, client, radio and input tests using host fakes |

There are no source patches, build-time source rewrites or replacement upstream
implementations. The compiler shim and enumerated linker wrappers are contained
in this module. Legacy compatibility reads the selected host's private `keys.h`
and `hci_core.h` declarations, with a Zephyr 3.5 version gate and key-layout
assertions. It is not an implementation-independent extension of Bluetooth.
The module does not read a parent keyboard
configuration or define Bluetooth connection, bond or PC-profile limits.
It does not pin, inspect or override an upstream Git revision. The containing
keyboard chooses its ZMK version through its existing West manifest.

## Integration

Add this module to the configuration of a compatible BLE split central. The
consuming keyboard supplies the board, shields, keymap and Bluetooth capacity.
This module supplies the receiver and Pair/Clear behaviors; it assigns no
physical keys and defines no keyboard layers.

This repository is the module root. `zephyr/module.yml` registers its CMake,
Kconfig and devicetree roots with Zephyr. It is built as part of a ZMK application;
it is not a separate firmware application and does not fetch or pin ZMK itself.

For a local checkout, append this CMake argument to your existing ZMK build:

```sh
-DZMK_EXTRA_MODULES=/absolute/path/to/zmk-ble-mouse
```

If other local modules are already supplied, append this checkout to that
semicolon-separated list. Include the keyboard configuration repository too when
it supplies custom boards or shields through its own `zephyr/module.yml`.
See ZMK's [external module build instructions](https://zmk.dev/docs/development/local-toolchain/build-flash#building-with-external-modules).

Add this project to your keyboard configuration's existing `config/west.yml`,
under `manifest.projects`:

```yaml
- name: zmk-ble-mouse
  url: https://github.com/bluepeng9/zmk-ble-mouse.git
  revision: master
```

Keep the existing ZMK and other dependency entries. Build firmware with the
keyboard's standard ZMK GitHub Actions workflow using this manifest. See ZMK's
[module setup instructions](https://zmk.dev/docs/features/modules#github-actions).
For local builds using the manifest, run `west update`; Zephyr discovers
`zephyr/module.yml` automatically. Use either the manifest checkout or a separate
local checkout through `ZMK_EXTRA_MODULES`, avoiding duplicate module entries.

In the central shield overlay, include:

```dts
#include <ble-mouse/receiver.dtsi>
```

On a compatible BLE split central, enable:

```conf
CONFIG_ZMK_POINTING=y
CONFIG_ZMK_BLE_MOUSE_CENTRAL=y
```

On the supported SC-only ZMK configuration,
`CONFIG_ZMK_BLE_MOUSE_LEGACY_PAIRING` defaults to `y`, so an existing receiver
integration picks up the change when this module is updated and the dongle is
rebuilt. No keyboard manifest fork or upstream source patch is needed. See the
compatibility contract below before flashing. Setting it to `n` restores the
original SC-only build and cannot pair a Legacy-only mouse.

SMP, settings and Zephyr's address-based auto-connect API are required. The latter
requires `CONFIG_BT_FILTER_ACCEPT_LIST` to remain disabled. The module does not
change that global option. The integration targets the existing Zephyr 3.5 / ZMK
v0.3+DYA APIs; independence from source edits does not imply compatibility with
every future API version.

### Legacy pairing compatibility contract

ZMK 0.3 selects `BT_SMP_SC_PAIR_ONLY`, excluding Legacy SMP code even when the
mouse client requests encrypted security level L2. A `.conf` assignment to `n`
cannot undo that Kconfig `select`. The module uses GCC's `-include` after Zephyr's
`-imacros autoconf.h` to undefine only that macro. It applies to the entire image,
not just `smp.c`, because Bluetooth key structure layout changes too. The
generated `.config` still reports `CONFIG_BT_SMP_SC_PAIR_ONLY=y`; the CMake status
message and build verifier report the effective compatibility mode explicitly.

This option enables the following guards, all owned by this module:

- `bt_conn_auth_cb_register`: preserves the application's callback table,
  including occupied-profile checks and I/O callbacks, and adds an admission
  check. Legacy pairing is accepted only for the module's selected new M720,
  in the 60-second Pair window, on a central connection with the default identity.
  Saved mouse bonds reconnect normally; a missing/stale bond must be cleared
  explicitly instead of silently paired again. Other devices still require SC.
- `bt_smp_init`: retains the original requirement for local SC commands before
  the Bluetooth host starts. A peer's SC flag alone cannot weaken that policy.
- `settings_call_set_handler`: expands old SC-only bond records in memory before
  the original settings handler reads them. It does not rewrite flash at boot.
  Direct/raw settings reads, unrelated subtrees and deletions are unchanged.
  Invalid short/unknown records are refused before upstream can delete them.
- `settings_save_one`: writes SC PC/keyboard bonds in their original short format;
  Legacy mouse records retain the full format. SC records therefore remain
  readable by the previous SC-only firmware. A downgrade cannot use the Legacy
  mouse bond and may discard that mouse record; it requires re-pairing on upgrade.

Authentication callback tables are immutable once registered, matching the
supported ZMK application's single static registration. A second registration
returns `-EALREADY`; callback removal and per-connection auth overlays return
`-ENOTSUP`. Overlays, including a NULL overlay, would bypass the admission guard.
An immutable SC-only table protects startup connections before ZMK registration.
Applications requiring dynamic auth replacement or overlays need a new integration.

Supported builds use GCC/GNU wrapping, Zephyr 3.5, SMP application admission and
Bluetooth settings. LTO, BR/EDR, SC-only security level L4 mode, OOB-only Legacy,
unauthenticated bond overwrite and automatic oldest-bond eviction are refused.
Private header/API or storage layout changes require review, even within 3.5.
The only device-name filter is discovery selection; it does not authenticate the
identity of a device advertising an M720 name.

Before flashing, build with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and run:

```sh
python3 scripts/verify_build.py /path/to/build --require-enabled
```

The verifier requires every compilation to include the compatibility header and
the final policy/storage wrapper definitions to exist. Actual call routing,
pairing success, bond restoration and USB/BLE output still need hardware testing.
Pairing/security failures now log the Zephyr reason code when logging is enabled;
enabling only the mouse log level does not enable the global logging backend.

### Assign controls in your keyboard keymap

Include the behavior definitions in the consuming keyboard's `.keymap` file:

```dts
#include <behaviors/ble-mouse.dtsi>
```

Place these bindings at two key positions of your choice in an existing layer's
`bindings` list, preserving the number of positions required by your layout:

| Binding | Action |
| --- | --- |
| `&ble_mouse MOUSE_PAIR` | Press to open a 60-second pairing window, or request another connection attempt to the saved mouse. |
| `&ble_mouse MOUSE_CLEAR` | Hold for two seconds to forget only the mouse; a short press does nothing. |

The layer name and physical key positions are chosen by your keyboard keymap.
If Studio overrides the compiled keymap, assign the BLE Mouse Pair/Clear
behaviors there. The behaviors resolve in shared peripheral keymaps, but the
receiver builds only on the central.

### Pair and use the mouse

After building and flashing the consuming keyboard's firmware:

1. Connect all enabled split peripherals to the central. Initial active discovery
   waits until they are connected.
2. Select an M720 Easy-Switch channel and hold its connect button until it flashes
   rapidly.
3. Press the key you assigned to `&ble_mouse MOUSE_PAIR`.
4. Keep that mouse channel selected. Use the keyboard's existing OUT and Bluetooth
   profile controls to select the destination for keyboard and mouse together.

A saved mouse reconnects automatically. Hold the key assigned to
`&ble_mouse MOUSE_CLEAR` for two seconds before replacing the mouse. Release
mouse buttons before changing OUT; see the output limitations below.

## Preserving split battery fetching

The current ZMK Bluetooth split disconnect callback obtains a peripheral slot
index for **every** disconnected BLE connection. For a mouse, no split slot
exists. When battery fetching is enabled it still queues that index as a battery
source. The central event handler then indexes `peripheral_battery_levels[source]`
without validating the source. See the inspected upstream
[disconnect callback](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/src/split/bluetooth/central.c#L1109)
and [central event handler](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/src/split/central.c).
These immutable links identify the source inspected for this issue; they do not
impose a build dependency or a required commit.

This is an existing upstream defect, not a lack of keyboard support in ZMK.
When `CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`, the module adds
`split_battery_guard.c` and a GNU linker
[`--wrap` flag](https://sourceware.org/binutils/docs/ld/Options.html) for
`zmk_split_transport_central_peripheral_event_handler`. External calls from the
Bluetooth transport go through the wrapper, which rejects battery sources outside
`ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT` with `-EINVAL`. That public macro is also the
bound of the upstream battery array; no extra slot is allocated.

Valid keyboard battery updates, including a zero on keyboard disconnect, and all
other event types are forwarded to the original handler with their arguments and
return values preserved. The upstream source files, keyboard battery setting,
bonds and connection limits stay intact. The receiver starts with battery
fetching enabled.
This guard concerns keyboard battery reception, not reporting the M720 battery.

This is an explicit dependency on one ZMK function and its external call boundary.
It does not require a particular Git revision, but changes to that function's
signature, array bound or call path require review. GNU wrapping acts on external
symbol references; inlining across translation units can bypass it. Validate the
final firmware linkage when changing toolchain or enabling link-time optimization.

## Connections and output semantics

The mouse consumes one existing free bond/connection and never evicts another
device. A full bond pool refuses pairing. Initial discovery matches the M720 name;
reconnection targets the saved peer through background auto-connect, which yields
to explicit keyboard scans. The module only stops a scan that it started itself.
It observes split transport status through the public API, leaving its status
callback and enable state alone.

The radio observer relies on Zephyr 3.5's reverse dynamic callback registration
order to yield an owned pairing scan before the split disconnect callback. Scan
start racing a keyboard disconnect, radio timing and simultaneous reconnections
still require controller/hardware validation. Host sequencing tests do not prove
those timing guarantees.

The standard input listener supplies pointing activity and the same selected OUT
as the keyboard. The module tracks only its own button edges, so its disconnect
or queue overflow does not release a button held by a keyboard mouse key.

On an endpoint-change event it discards its own pending frames, releases its own
forwarded state through the input listener, and suppresses buttons that were held
at the change until they are released and pressed again. **The public event
arrives after OUT changes.** The module cannot release a button on the previous
BLE host or purge frames already submitted to upstream input/HOG queues. This is
the stock ZMK output behavior. Release mouse buttons before switching OUT during hardware
testing. In-flight frames may follow the newly selected endpoint.

## Tests and remaining validation

Using Python 3 (standard library only), a C11 compiler and a linker supporting
GNU `--wrap`:

```sh
python3 tests/run_tests.py --sanitize
python3 tests/check_module.py
```

Check a firmware build's receiver settings without a keyboard-specific wrapper:

```sh
python3 scripts/verify_build.py /path/to/build --require-enabled
```

This reads the generated `.config` and, with battery fetching enabled, checks the
final map (`zephyr/zmk.map` when `CONFIG_KERNEL_BIN_NAME="zmk"`) for both the
wrapper and original handler definitions.
With Legacy compatibility enabled it also checks the auth/SMP/storage symbols
and `compile_commands.json` for consistent compiler configuration.
It does not read West manifests or Git state. Symbol presence alone does not prove
call routing on the device; retain the firmware/hardware checks below.

Sanitizers require a supported host such as Linux; omit `--sanitize` on Windows.
`--cc` chooses a compiler command, for example `--cc gcc`. Windows GCC from
w64devkit supports the linker test; put its `bin` directory on `PATH`. Zig's
Windows linker does not support the required `--wrap` flag.
`.github/workflows/test.yml` runs these checks for this repository on push and
pull request, and supports manual runs.

Host tests compile production parser, GATT client, output worker and scanner
coordinator. They cover 20,000 descriptor mutations, fragmented discovery, bond
capacity, setup failures, reconnection, cancellation without a disconnect callback,
Clear hold duration, button ownership, overflow, pending endpoint resets and
reentrant input callbacks. A separate test calls the public battery handler through the
actual linker wrapper, rejects every invalid 8-bit source, and checks preservation
of keyboard battery values, disconnect updates and other event payloads.
Auth, local SC capability and bond adapters are exercised through real GNU linker
wrapping. Bond tests cover signed/unsigned record layouts, old SC and new Legacy
records, failed/short reads, raw/subtree loads, deletion and storage errors.

Host tests passed on Windows with GCC. A full nice_nano_v2 image with
`eyelash_sofle_central_dongle dongle_display`, Studio USB RPC, Zephyr 3.5 and
Zephyr SDK 0.16.8/GCC 12.2 builds successfully. Its final image contains Legacy
SMP code, and disassembly confirms auth registration, SMP init, NVS loading,
Bluetooth key saving and split events call the intended wrappers. The local
Windows build uses `-ULV_CONF_PATH` to select the existing LVGL include directory
without expanding the drive letter as ZMK's `C` keycode macro; no upstream file
is edited for that build adjustment.

Actual upstream input/HOG behavior and physical M720 validation remain
outstanding. Test USB/BLE
movement, five buttons, wheel/tilt,
OUT changes, keyboard mouse keys, all configured split peripherals reconnecting,
mouse sleep/wake, restart, Clear and full bond capacity. Confirm that each
configured keyboard peripheral's battery value continues updating when the mouse
connects, disconnects and reconnects.
