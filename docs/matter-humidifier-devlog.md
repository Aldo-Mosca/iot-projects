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
| 5V relay module                       | On hand       | Switch misting element or fan via ESP32 GPIO    |
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
│   ├── esp-idf/          ← ESP-IDF framework
│   └── esp-matter/       ← ESP Matter SDK
├── Matter-Humidifier/    ← Humidifier project (future)
└── swift-matter-examples/ ← Apple's Embedded Swift + Matter examples
```

### Shell Aliases (`~/.zshrc`)

```zsh
alias get_idf='. $HOME/Local-Documents/repos/IoT-projects/esp/esp-idf/export.sh'
alias get_matter='. $HOME/Local-Documents/repos/IoT-projects/esp/esp-matter/export.sh'
alias get_esp='. $HOME/Local-Documents/repos/IoT-projects/esp/esp-idf/export.sh && . $HOME/Local-Documents/repos/IoT-projects/esp/esp-matter/export.sh'
```

### ESP-IDF

- **Recommended version for esp-matter**: v5.4.1
- Installed at: `$HOME/Local-Documents/repos/IoT-projects/esp/esp-idf`
- Python env: `~/.espressif/python_env/idf5.4_py3.14_env`

### ESP Matter SDK

- **Recommended IDF**: v5.4.1
- Installed at: `$HOME/Local-Documents/repos/IoT-projects/esp/esp-matter`
- Cloned with `--depth 1`, submodules initialized
- `connectedhomeip` submodule checked out with `--platform esp32 darwin --shallow`
- `install.sh` completed successfully

### Verified Working: C++ Matter Firmware

- Built and flashed `examples/light` from esp-matter ✅
- Device commissioned into Apple Home using passcode `20202021` ✅
- Apple Home can see and toggle device state ✅
- LEDs don't respond (expected — GPIO assignments don't match XIAO pinout)
- Matter-over-WiFi confirmed working; Thread also available

-----

## Embedded Swift Exploration (Working ✅)

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
get_esp                          # Source both IDF and Matter
idf.py set-target esp32c6
# Explicitly pass nightly toolchain — Xcode Swift will be used otherwise
export TOOLCHAINS=$(plutil -extract CFBundleIdentifier raw /Library/Developer/Toolchains/swift-latest.xctoolchain/Info.plist)
idf.py build flash monitor
```

### Issues Encountered

| Issue                                                   | Cause                                                           | Status                                                            |
|---------------------------------------------------------|-----------------------------------------------------------------|-------------------------------------------------------------------|
| `riscv32-none-none-eabi` target not found               | Apple's Xcode Swift used instead of nightly                     | ✅ Fixed: explicit TOOLCHAINS env var                              |
| `-fpic`/`-fpie` linker error                            | IDF version mismatch                                            | ✅ Fixed: use IDF v5.2.1 with esp-matter release/v1.2             |
| `esp_matter.endpoint has no member get_device_type_ids` | esp-matter API changed; Swift code written against older commit | ✅ Fixed: pin esp-matter to `release/v1.2` branch                 |
| `pigweed_environment.gni` not found                     | `install.sh` never run; file not generated                      | ✅ Fixed: create empty stub `declare_args() {}` — see ref doc     |
| CMake 4.x `ExternalProject_Add` `cd "src;bin"` error   | CMake 4.x broke `WORKING_DIRECTORY` with separate `SOURCE_DIR`/`BINARY_DIR` | ✅ Fixed: remove `WORKING_DIRECTORY`, use `cmake -E chdir` in `BUILD_COMMAND` — see ref doc |
| `zap-cli` not found during GN build                    | ZAP code generator not installed                                | ✅ Fixed: `cipd install fuchsia/third_party/zap/mac-amd64`        |
| Swift PCH: `riscv/rv_utils.h` not found                | IDF component include dirs not passed to Swift `-Xcc -I` flags  | ✅ Fixed: loop over `__COMPONENT_INCLUDE_DIRS` in `main/CMakeLists.txt` — see ref doc |
| mbedtls CMake conflict                                  | esp-matter incompatible with IDF v5.5.1                         | ✅ Fixed: use IDF v5.2.1 (also works with v5.4.1)                 |

### Version Compatibility Matrix

| Component             | Version                                 | Notes                                                    |
|-----------------------|-----------------------------------------|----------------------------------------------------------|
| ESP-IDF               | v5.2.1 ✅ (v5.4.1 also works)           | v5.5.1 breaks mbedtls; v5.2.1 confirmed working 2026-03-30 |
| esp-matter            | `release/v1.2` ✅                       | `get_device_type_ids` still present; removed in later branches |
| Swift toolchain       | Nightly trunk snapshot                  | Not Xcode Swift                                          |
| swift-matter-examples | main branch                             | Written against esp-matter ~mid-2024                     |

### ~~Current Blocker~~ — Resolved ✅

~~The `swift-matter-examples` Swift code calls `esp_matter.endpoint.get_device_type_ids()` which was removed in a recent esp-matter commit.~~

**Resolution (2026-03-30):** Pinning esp-matter to the `release/v1.2` branch preserves the `get_device_type_ids` API. No need to bisect by commit. The `smart-light` example built, flashed, and commissioned into Apple Home successfully with this branch.

For reference, the relevant esp-matter commits were:
- `18c5d4a5` — `get_device_type_ids` added
- `9d7ff306` — `get_device_type_ids` removed

-----

## Next Steps

### Hardware

- [ ] Source compact buck converter (MP1584 or LM2596-based, 12V→5V, ≥1A)
- [ ] Wire relay module into bench breadboard
- [ ] Test relay switching via XIAO GPIO
- [ ] Probe VRK connectors (3 & 5) while humidifier is actively misting — measure signal voltage and frequency

### Firmware (C++ path — proven working)

- [ ] Create humidifier project in `Matter-Humidifier/`
- [ ] Replace light endpoint with Matter humidifier device type
- [ ] Wire attribute callbacks to relay GPIO output
- [ ] Test misting on/off via Apple Home

### Firmware (Embedded Swift path — unblocked ✅)

- [x] Resolve esp-matter version/commit mismatch with swift-matter-examples — pin to `release/v1.2`
- [ ] Get `led-blink` example building and flashing cleanly
- [x] Get `smart-light` example building and commissioning into Apple Home ✅ 2026-03-30
- [ ] Port humidifier logic to Swift once examples are working

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
- **Relay module for VRK control**: Rather than replacing the green board, intercept the VRK control signal via relay.
- **Embedded Swift goal**: Write application-level code in Swift over the ESP Matter C++ SDK — cutting edge as of 2024-2026, toolchain still maturing.
