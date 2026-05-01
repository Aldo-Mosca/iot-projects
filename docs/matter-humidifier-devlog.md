# Matter Humidifier — Development Log

## Project Goal

Add Matter smart home capabilities to a simple room humidifier using a Seeed Studio XIAO ESP32-C6 module. Target ecosystem: **Apple Home only** (multi-ecosystem certification is out of scope). The ESP32-C6's native Thread (802.15.4) support makes it well-suited for Matter-over-Thread, which works natively with Apple Home infrastructure (HomePod mini, Apple TV 4K act as Thread Border Routers automatically).

-----

## Hardware

### Microcontroller

- **Seeed Studio XIAO ESP32-C6** (bare module, XIAO form factor)
  - USB-C for programming
  - Built-in antenna
  - 3.3V logic on all GPIOs
  - Can be powered via 5V pin
  - Native Thread (802.15.4) + Wi-Fi 6 + BLE

### Humidifier Internals

Two internal boards identified:

**Yellow board** — Power supply/driver

- Contains transformer, filter capacitors, inductors, rectifier
- Converts AC mains to DC rails
- Labeled "POWER SUPPLY" / manufacturer: hanny.com.cn

**Green board** — Control board (mounted behind two-button panel)

- Handles user input (power + mode buttons)
- Controls all outputs

**Voltage rails measured (humidifier powered on):**

| Rail                   | Voltage              | Notes                       |
|------------------------|----------------------|-----------------------------|
| Fan                    | 11.14V               | Steady DC                   |
| LED output             | 10.32V               | Steady DC                   |
| To LEDs                | ~1.5V                | LED forward voltage         |
| VRK (connectors 3 & 5) | ~12V switching to 0V | Switched/PWM control signal |

**No clean 5V rail exists** — a buck converter is required to power the XIAO.

### Green Board Connector Map (left to right)

1. **LED power** — 10.32V from yellow board "LED" socket
1. **Fan power** — 11.14V
1. **To VRK** — switching control signal to ultrasonic transducer
1. **H₂O level sensor** — water level killswitch
1. **To VRK** — second VRK control line

### "VRK" Component

Label on yellow board for the **ultrasonic misting transducer circuit**. The switching signal on connectors 3 & 5 is the control signal for misting — this is the key signal to intercept with the ESP32.

-----

## Parts Inventory

| Part                                  | Status        | Role                                            |
|---------------------------------------|---------------|-------------------------------------------------|
| Seeed Studio XIAO ESP32-C6            | On hand       | Primary MCU                                     |
| MB102 breadboard power supply         | On hand       | Bench prototyping (too large for final install) |
| 5V relay module                       | On hand       | ~~Switch misting element~~ Not needed — button shunt approach used instead |
| MP1584 or LM2596-based buck converter | **To source** | Step down 11V→5V for final installation         |
| 4x AA battery pack (used 6x AA = 9V)  | On hand       | Powers MB102 for bench testing                  |

### Bench Setup (Current State)

- XIAO ESP32-C6 seated on breadboard
- MB102 powered by 6x AA batteries (9V)
- MB102 jumpers set to 5V output
- XIAO powered via 5V pin from MB102 rails
- Matter firmware running, device visible and controllable in Apple Home ✅

-----

## Software Stack

### Host Environment

- **macOS** (Apple Silicon — M-series)
- Python 3.14.3 (native ARM64 via Homebrew at `/opt/homebrew`)
- Homebrew at `/opt/homebrew` (native Apple Silicon)
- Xcode Command Line Tools installed
- Rosetta present but not interfering

### Directory Structure

```
$HOME/Local-Documents/repos/IoT-projects/
├── esp/
│   ├── esp-idf/             ← ESP-IDF v5.2.1 (old, C++ light example)
│   ├── esp-idf-v5.4.1/      ← ESP-IDF v5.4.1 (current, Embedded Swift)
│   ├── esp-matter/          ← esp-matter release/v1.2 (old)
│   └── esp-matter-v1.4/     ← esp-matter release/v1.4 (current, Embedded Swift)
├── Matter-Humidifier/       ← Humidifier project (active)
└── swift-matter-examples/   ← Apple's Embedded Swift + Matter examples
```

### Shell Aliases (`~/.zshrc`)

```zsh
# ESP-IDF v5.2.1 + esp-matter v1.2 (C++ light example, kept for reference)
alias get_esp='source ~/Local-Documents/repos/IoT-projects/esp/esp-idf/export.sh && \
  source ~/Local-Documents/repos/IoT-projects/esp/esp-matter/export.sh'

# ESP-IDF v5.4.1 + esp-matter v1.4 (use this for Matter-Humidifier)
alias get_esp541='source ~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1/export.sh && \
  source ~/Local-Documents/repos/IoT-projects/esp/esp-matter-v1.4/export.sh'
```

### ESP-IDF

Two installs coexist, sharing `~/.espressif` (safe — IDF namespaces by version internally):

| Version | Path | Python env |
|---------|------|------------|
| v5.2.1 | `esp/esp-idf` | `~/.espressif/python_env/idf5.2_py3.14_env` |
| v5.4.1 | `esp/esp-idf-v5.4.1` | `~/.espressif/python_env/idf5.4_py3.14_env` |

### ESP Matter SDK

| Version | Path | Paired IDF |
|---------|------|------------|
| release/v1.2 | `esp/esp-matter` | v5.2.1 |
| release/v1.4 | `esp/esp-matter-v1.4` | v5.4.1 |

See [`SETUP_ESP541_ENV.md`](./SETUP_ESP541_ENV.md) for the v5.4.1 + v1.4 setup procedure and known pitfalls (PEP 668, `--no-host-tool`, `hash -r`).

### Verified Working: C++ Matter Firmware

- Built and flashed `examples/light` from esp-matter ✅
- Device commissioned into Apple Home using passcode `20202021` ✅
- Apple Home can see and toggle device state ✅
- LEDs don't respond (expected — GPIO assignments don't match XIAO pinout)
- Matter-over-WiFi confirmed working; Thread also available

-----

## Embedded Swift Exploration (Working ✅)

> **Session update — 2026-04-18 (2)**: Switched to Fan device type (`0x002B`). Replaced On/Off Plug-in Unit endpoint with Fan Control cluster. Full 4-state machine (`Off/High/Low/Night`) with K1 press-count logic, bidirectional Matter sync, and physical button press detection via ISR-latched GPIO. Night maps to FanMode=Low (intentionally lossy; future OnOff cluster for LED will distinguish). See Fan Device Type section below.

> **Session update — 2026-04-18**: Replaced relay approach with GPIO-to-button shunt. `Relay.swift` → `ButtonShunt.swift`. Uses open-drain GPIO to simulate a momentary button press across the green board's power button contacts. No relay module needed.

> **Session update — 2026-04-12**: Upgraded to ESP-IDF v5.4.1 + esp-matter release/v1.4. Scaffolded `Matter-Humidifier/` from `smart-light`. Fixed the `get_device_type_ids` API removal with a Swift-side registry. Wrote the full humidifier Swift layer (`MatterOnOffPluginUnit`, `Matter.Humidifier`, `Relay`, updated `Main.swift`). See details below and in [`SETUP_ESP541_ENV.md`](./SETUP_ESP541_ENV.md).

> **Session update — 2026-03-30**: All blockers below were resolved in a Claude Code session working through the `smart-light` example. Full details of every fix are in [`swift-matter-smart-light-xiao-esp32c6.md`](./swift-matter-smart-light-xiao-esp32c6.md) in this same docs folder.

### Goal

Write application-level firmware in **Embedded Swift** using Apple's `swift-matter-examples` repo, with the ESP Matter C++ SDK underneath.

### Repository

- `https://github.com/swiftlang/swift-matter-examples`
- Associated with WWDC 2024 session 10197: "Go small with Embedded Swift"
- Targets ESP32-C6 (RISC-V) specifically

### Swift Toolchain Requirements

- **Required**: Open-source Swift nightly/trunk development snapshot (NOT Apple's Xcode Swift)
- Apple's Swift (`swiftlang-6.2.4`) does NOT know about `riscv32-none-none-eabi` target
- Install via: https://www.swift.org/download → Trunk Development (main) snapshot
- Install to `/Library/Developer/Toolchains/`

### Critical Build Procedure (macOS)

```bash
# ALWAYS use a fresh terminal
get_esp541                       # Source IDF v5.4.1 + esp-matter v1.4
idf.py set-target esp32c6
# Explicitly pass nightly toolchain — Xcode Swift will be used otherwise
TOOLCHAINS=$(plutil -extract CFBundleIdentifier raw /Library/Developer/Toolchains/swift-latest.xctoolchain/Info.plist) idf.py build flash monitor
```

### Issues Encountered

| Issue                                                   | Cause                                                           | Status                                                            |
|---------------------------------------------------------|-----------------------------------------------------------------|-------------------------------------------------------------------|
| `riscv32-none-none-eabi` target not found               | Apple's Xcode Swift used instead of nightly                     | ✅ Fixed: explicit TOOLCHAINS env var                              |
| `-fpic`/`-fpie` linker error                            | IDF version mismatch                                            | ✅ Fixed: use IDF v5.2.1 with esp-matter release/v1.2             |
| `esp_matter.endpoint has no member get_device_type_ids` | esp-matter API changed; Swift code written against older commit | ✅ Fixed: registry approach (see below) — upgrading to v1.4       |
| `pigweed_environment.gni` not found                     | `install.sh` never run; file not generated                      | ✅ Fixed: create empty stub `declare_args() {}` — see ref doc     |
| CMake 4.x `ExternalProject_Add` `cd "src;bin"` error   | CMake 4.x broke `WORKING_DIRECTORY` with separate `SOURCE_DIR`/`BINARY_DIR` | ✅ Fixed: remove `WORKING_DIRECTORY`, use `cmake -E chdir` in `BUILD_COMMAND` — see ref doc |
| `zap-cli` not found during GN build                    | ZAP code generator not installed                                | ✅ Fixed: `cipd install fuchsia/third_party/zap/mac-amd64`        |
| Swift PCH: `riscv/rv_utils.h` not found                | IDF component include dirs not passed to Swift `-Xcc -I` flags  | ✅ Fixed: loop over `__COMPONENT_INCLUDE_DIRS` in `main/CMakeLists.txt` — see ref doc |
| mbedtls CMake conflict                                  | esp-matter incompatible with IDF v5.5.1                         | ✅ Fixed: use IDF v5.2.1 (also works with v5.4.1)                 |

### Version Compatibility Matrix

| Component             | Version                                 | Notes                                                    |
|-----------------------|-----------------------------------------|----------------------------------------------------------|
| ESP-IDF               | v5.4.1 ✅                               | Required by esp-matter v1.4; v5.2.1 kept for reference  |
| esp-matter            | `release/v1.4` ✅                       | Newest stable branch (Matter spec v1.4)                  |
| Swift toolchain       | Nightly trunk snapshot                  | Not Xcode Swift                                          |
| swift-matter-examples | main branch, adapted into Matter-Humidifier | Upstream not modified; humidifier forked from smart-light |

### `get_device_type_ids` — Resolved ✅ (2026-04-12)

The `swift-matter-examples` Swift code calls `esp_matter.endpoint.get_device_type_ids()` and `get_device_type_id()`, both removed from esp-matter after commit `9d7ff306`.

**Resolution (2026-03-30, v1.2 path):** Pinning to `release/v1.2` preserved the old API. The `smart-light` example built and commissioned into Apple Home with this branch.

**Resolution (2026-04-12, v1.4 path):** Upgraded to esp-matter `release/v1.4` + IDF v5.4.1 and replaced both removed calls with a Swift-side registry:

- `_endpointDeviceTypeRegistry: [UInt: UInt32]` — module-level dictionary in `Node.swift`, keyed by endpoint pointer address
- Each endpoint struct (`MatterExtendedColorLight`, `MatterOnOffPluginUnit`) registers its static device type ID into the dictionary immediately after `create()` returns
- `Endpoint.as<T>(_:)` looks up the registry instead of calling the deleted API
- `MatterExtendedColorLight.deviceTypeId` hardcoded to `0x010D` (Matter spec); `MatterOnOffPluginUnit.deviceTypeId` hardcoded to `0x010A`

For reference:
- `18c5d4a5` — `get_device_type_ids` added to esp-matter
- `9d7ff306` — `get_device_type_ids` removed

Note: the registry entry previously referred to `MatterExtendedColorLight` and `MatterOnOffPluginUnit`. Both have been replaced by `MatterFan` as of 2026-04-18.

### Fan Device Type — Design Notes (2026-04-18)

**Why Fan (`0x002B`) instead of On/Off Plug-in Unit (`0x010A`):** No native humidifier device type exists in Matter 1.4 or 1.5. The Fan device type is the industry workaround (used by SwitchBot, TCL). Apple Home renders it with speed/mode controls, which maps naturally to the humidifier's four states.

**Device type ID correction:** The task brief stated `0x0044`. The actual value in `esp_matter_endpoint.h` (esp-matter release/v1.4) is `0x002B`. Code uses `0x002B`.

**Hardware state machine:**
```
Off (0) → High (1) → Low (2) → Night (3) → Off (0)  [each K1 press advances one step]
```

**Matter FanMode mapping:**

| Hardware state | FanMode value | Label in Apple Home | Note |
|---|---|---|---|
| 0 Off   | 0 | Off  | |
| 1 High  | 3 | High | |
| 2 Low   | 1 | Low  | |
| 3 Night | 1 | Low  | lossy — intentional |

Night maps to FanMode=Low (lossy). The internal `hwState` variable still tracks Night (3) correctly, so physical button press round-trips are accurate. A future OnOff cluster for the LED will distinguish Low (fan=low, LED=on) from Night (fan=low, LED=off) without changing this mapping.

**FanModeSequence:** Set to `0x01` (`kOffLowHigh`). The task brief suggested `0x03` (`kOffLowHighAuto`), but the actual ZAP-generated enum (`cluster-enums.h`) shows `0x01 = kOffLowHigh` is the correct value for a three-mode Off/Low/High sequence with no Auto.

**K1 press-count logic:** Apple Home sends a target FanMode; firmware maps it to a target hardware state, then calculates:
```
presses = (targetHw - hwState + 4) % 4
```
Presses are fired with 200 ms between each. Logic lives in `Main.swift`, not in the C++ shim.

**Physical button detection:** A second GPIO configured as input with falling-edge ISR latches a `volatile bool`. The main loop polls `matter_button_was_pressed()` every 200 ms, advances `hwState`, and calls `fanEndpoint.updateMode()` to push the new FanMode back to Matter. ISR setup in `MatterInterface.cpp` to keep IRAM-safe C code out of Swift.

**`matter_fan_update_mode` shim:** Calls `esp_matter::attribute::update(endpoint_id, 0x0202, 0x0000, &val)` with `esp_matter_enum8(fan_mode)` as the value. `attribute::update()` signature confirmed from `esp_matter_attribute_utils.h`.

**Files changed:**
- `Matter/MatterInterface.h` — added `matter_fan_update_mode`, `setup_button_listen_gpio`, `matter_button_was_pressed`
- `Matter/MatterInterface.cpp` — implemented above; ISR handler marked `IRAM_ATTR`
- `Matter/Clusters.swift` — added `FanControl` struct, `ClusterID.fanControl = 0x0202`
- `Matter/Attribute.swift` — added `FanControl.FanModeValue`
- `Matter/Matter.swift` — replaced `ExtendedColorLight`/`Humidifier` with `Matter.Fan`; added `.fanMode` to `Endpoint.Attribute` enum
- `Matter/Node.swift` — replaced `MatterExtendedColorLight`/`MatterOnOffPluginUnit` with `MatterFan` (device type `0x002B`)
- `main/ButtonShunt.swift` — added `buttonListenGPIO` constant (GPIO5, TODO: confirm)
- `main/Main.swift` — full state machine with mapping tables, press-count logic, 200 ms polling loop

### Button Shunt — Design Notes (2026-04-18)

**Why not relay:** The original plan was to intercept the VRK switching signal with a 5V relay. Discarded in favour of simulating a button press on the green board's control panel — simpler wiring, no relay module required, and the green board's MCU stays in charge of the humidifier state machine (water level sensor, safety logic, etc.).

**Wiring:** One XIAO GPIO pin connected across the two contacts of the green board's power button. The button is assumed to be a simple momentary switch to GND (standard for small appliances). When the GPIO is driven low, the green board's MCU sees an identical signal to a physical button press.

**GPIO mode — open-drain (`GPIO_MODE_OUTPUT_OD`):**
- Write `0` → GPIO actively pulls the line low → simulates press
- Write `1` → GPIO is high-impedance (floating) → simulates release

Open-drain is important: if the button has a pull-up resistor on the green board (almost certainly), a push-pull output driving high would fight it. Open-drain avoids the conflict entirely.

**`delay_ms` shim:** `pdMS_TO_TICKS` is a C preprocessor macro and cannot be called from Swift. A thin C++ wrapper `delay_ms(uint32_t ms)` was added to `MatterInterface.h/.cpp` following the same pattern as `recomissionFabric()`.

**State tracking:** The XIAO has no feedback line from the green board, so it cannot read the humidifier's actual on/off state. `humidifierIsOn` in `Main.swift` tracks the assumed state. The press is only triggered when the desired Matter state differs from the assumed state — avoiding double-presses on redundant attribute updates. Known limitation: if the physical button is pressed manually, or power is cycled, the assumed state drifts. Toggling twice in Apple Home re-syncs it.

**Files changed:**
- `Matter/MatterInterface.h` — added `delay_ms` declaration
- `Matter/MatterInterface.cpp` — added `delay_ms` implementation
- `main/ButtonShunt.swift` — new file (replaces `Relay.swift`)
- `main/Main.swift` — state-tracking press logic
- `main/CMakeLists.txt` — `Relay.swift` → `ButtonShunt.swift`

-----

## Next Steps

### Hardware

- [ ] Source compact buck converter (MP1584 or LM2596-based, 12V→5V, ≥1A)
- [ ] **Confirm GPIO pins** — update `powerButtonGPIO` and `buttonListenGPIO` in `ButtonShunt.swift` (currently GPIO4 / GPIO5, unverified)
- [ ] Wire output GPIO (open-drain) across K1 button contacts on green board
- [ ] Wire input GPIO to K1 button line for physical press detection
- [ ] Test button press simulation and physical press detection

### Firmware (Embedded Swift path — code complete, not yet flashed)

- [x] Resolve esp-matter version/commit mismatch — upgraded to release/v1.4 ✅ 2026-04-12
- [x] Get `smart-light` example building and commissioning into Apple Home ✅ 2026-03-30
- [x] Scaffold `Matter-Humidifier/` from `smart-light` ✅ 2026-04-12
- [x] Fix `get_device_type_ids` removal with Swift registry approach ✅ 2026-04-12
- [x] Switch to Fan device type (`0x002B`), Fan Control cluster (`0x0202`) ✅ 2026-04-18
- [x] 4-state machine with K1 press-count logic and physical button sync ✅ 2026-04-18
- [x] Button shunt (open-drain output) + physical press detection (ISR-latched input) ✅ 2026-04-18
- [ ] **Confirm GPIO pin numbers and wire up**
- [ ] **Build** — `get_esp541 && idf.py set-target esp32c6 && TOOLCHAINS=<nightly> idf.py build`
- [ ] Flash and commission into Apple Home as Fan device
- [ ] Test all four states (Off / High / Low / Night) via Apple Home and physical button

### Notes App → DEVLOG workflow

- Take notes on iPad in Apple Notes
- Copy relevant notes into this file on Mac
- Claude Code can reference this file for project context

-----

## Key Decisions & Rationale

- **ESP32-C6 over ESP32-C3**: C6 has native Thread (802.15.4); C3 is WiFi-only. Thread is preferred for Matter in Apple Home ecosystem.
- **Apple Home only**: Multi-ecosystem testing is out of scope. Matter's write-once-works-everywhere promise means the implementation won't change for other platforms.
- **Hardware-first sequencing**: Map all voltages and control signals before writing firmware, to avoid surprises later.
- **No 5V rail in humidifier**: Must use buck converter for final installation. MB102 is bench-only.
- **Button shunt over relay**: Rather than intercepting the VRK signal with a relay, the XIAO simulates power button presses on the green board using a single open-drain GPIO shunted across the button contacts. The green board's MCU stays in charge of the humidifier state machine. Simpler wiring, no relay module required.
- **Fan device type over On/Off Plug-in Unit**: Matter 1.4/1.5 has no humidifier device type. Fan (`0x002B`) is the industry workaround — Apple Home renders speed/mode controls that map naturally to the four humidifier states. On/Off would lose mode visibility.
- **Night → Low mapping (intentionally lossy)**: Matter's FanMode enum has no Night equivalent. Night (low fan, LED off) maps to FanMode=Low. The firmware tracks the real hardware state internally. A future OnOff cluster for the LED will let Apple Home distinguish Low (LED on) from Night (LED off) without changing this mapping.
- **Embedded Swift goal**: Write application-level code in Swift over the ESP Matter C++ SDK — cutting edge as of 2024-2026, toolchain still maturing.
