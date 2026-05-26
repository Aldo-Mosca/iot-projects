# Matter Humidifier — Development Log

## Project Goal

Add Matter smart home capabilities to a simple room humidifier using a Seeed Studio XIAO ESP32-C6 module. Target ecosystem: **Apple Home only** (multi-ecosystem certification is out of scope). The ESP32-C6's native Thread (802.15.4) support makes it well-suited for Matter-over-Thread, which works natively with Apple Home infrastructure (HomePod mini, Apple TV 4K act as Thread Border Routers automatically).

---

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
| Rail | Voltage | Notes |
|------|---------|-------|
| Fan | 11.14V | Steady DC |
| LED output | 10.32V | Steady DC |
| To LEDs | ~1.5V | LED forward voltage |
| VRK (connectors 3 & 5) | ~12V switching to 0V | Switched/PWM control signal |

**No clean 5V rail exists** — a buck converter is required to power the XIAO.

### Green Board Connector Map (left to right)
1. **LED power** — 10.32V from yellow board "LED" socket
2. **Fan power** — 11.14V
3. **To VRK** — switching control signal to ultrasonic transducer
4. **H₂O level sensor** — water level killswitch
5. **To VRK** — second VRK control line

### "VRK" Component
Label on yellow board for the **ultrasonic misting transducer circuit**. The switching signal on connectors 3 & 5 is the control signal for misting — this is the key signal to intercept with the ESP32.

### Green Board Button Detail (confirmed by disassembly)
Board ID: `FY-TT-10012-1` / `KB-3151C` / manufacturer: hanny.com.cn

| Label | Function | Phase |
|-------|----------|-------|
| K1 | Humidifier mode button — cycles Hi → Low → Night → Off | Phase 1 |
| K2 | RGB LED color cycle button | Phase 2 |
| LED5 | Indicator: High mode active |  |
| LED6 | Indicator: Low mode active |  |
| LED7 | Indicator: Night mode active |  |
| U5 (EL817) | Optocoupler — isolates mains-side signals from logic |  |
| BUZ | Buzzer circuit |  |

**Button circuit behavior:**
- Both K1 legs measure ~0V DC — lines are pulled to ground
- Direct GPIO connection to button terminals causes **loading effect** — disrupts button detection on green board MCU and prevents physical button presses from registering
- Solution: use an N-channel MOSFET to briefly short the two K1 terminals, mimicking a physical press without loading the circuit

### Button Shunting — MOSFET Wiring
```
ESP32 GPIO (D0) → 1kΩ resistor → Gate
Drain → one K1 terminal
Source → other K1 terminal
```
When GPIO goes high, MOSFET saturates and shorts K1 terminals — clean button press simulation.

**MOSFETs used (through-hole, breadboard friendly):**
- **BS170** — N-channel, TO-92 package ✅
- **2N7000** — N-channel, TO-92 package ✅
- AO3400A (SOT-23 SMD) — ordered but not used; requires breakout board for breadboarding

---

## Parts Inventory

| Part | Status | Role |
|------|--------|------|
| Seeed Studio XIAO ESP32-C6 | On hand | Primary MCU |
| MB102 breadboard power supply | On hand | Bench prototyping (too large for final install) |
| 5V relay module | On hand | Available but not used — MOSFET approach preferred |
| BS170 / 2N7000 N-channel MOSFETs | On hand | Button press simulation via K1/K2 shunting |
| AO3400A / AO3401A MOSFETs | On hand | SMD — require SOT-23 breakout board for breadboarding |
| MP1584 or LM2596-based buck converter | **To source** | Step down 11V→5V for final installation |
| 6x AA battery pack (9V) | On hand | Powers MB102 for bench testing |

### Bench Setup (Current State)
- XIAO ESP32-C6 seated on breadboard
- MB102 powered by 6x AA batteries (9V)
- MB102 jumpers set to 5V output
- XIAO powered via 5V pin from MB102 rails
- BS170/2N7000 MOSFET wired to K1 terminals for button simulation
- GPIO D0 = output (button simulation), GPIO D1 = input (physical press detection)
- Matter firmware running, device commissioned and controllable in Apple Home ✅

---

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

### Verified Working: Embedded Swift Humidifier Firmware ✅
- Built on `swift-matter-examples` architecture (WWDC 2024 session 10197)
- Matter Fan device type (0x0044) — industry workaround; no native humidifier device type in Matter 1.4
- FanModeSequence = 0x01 (Off/Low/High) — required to show mode selector in Apple Home
- Apple Home sends PercentSetting writes; code maps percentage ranges to hardware states
- K1 button simulation via MOSFET confirmed working ✅
- Device commissioned into Apple Home ✅
- Mode changes from Apple Home trigger correct number of K1 presses ✅
- Physical K1 presses detected and synced back to Matter data model ✅

**Fan mode mapping:**
| Apple Home % | Hardware State | FanMode Value |
|-------------|---------------|---------------|
| 0% | Off | 0 |
| 1–49% | Low | 1 |
| 50–83% | High | 3 |
| 84–100% | Night | 2 |

**Key firmware files:**
- `Matter/MatterInterface.h` / `.cpp` — C++ shim layer
- `Matter/Matter.swift` — Swift Matter abstraction (Node, Endpoint, Fan classes)
- `main/Main.swift` — Application logic, mode mapping, press count calculation
- `main/ButtonShunt.swift` — GPIO output for K1 simulation (D0 = GPIO0, D1 = GPIO1)

**Commissioning passcode:** `20202021`

**Gotchas:**
- Wrong WiFi network will cause `Error ESP32:0x0500300F` during commissioning — check network before debugging firmware
- Full flash erase required when switching between firmware versions: `idf.py erase-flash`

---

## Embedded Swift Exploration (In Progress)

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

| Issue | Cause | Status |
|-------|-------|--------|
| `riscv32-none-none-eabi` target not found | Apple's Xcode Swift used instead of nightly | Fixed: explicit TOOLCHAINS env var |
| `-fpic`/`-fpie` linker error | IDF version mismatch | Investigated |
| `esp_matter.endpoint has no member get_device_type_ids` | esp-matter API changed; Swift code written against older commit | In progress |
| `pigweed_environment.gni` not found | Missing submodule | Fixed: `checkout_submodules.py --platform esp32 darwin --shallow` |
| mbedtls CMake conflict | esp-matter incompatible with IDF v5.5.1 | Fixed: downgrade to v5.4.1 |

### Version Compatibility Matrix
| Component | Version | Notes |
|-----------|---------|-------|
| ESP-IDF | v5.4.1 | Required by esp-matter |
| esp-matter | v1.2 (per swift-matter-examples README) | `get_device_type_ids` API removed in later versions |
| Swift toolchain | Nightly trunk snapshot | Not Xcode Swift |
| swift-matter-examples | main branch | Written against esp-matter ~mid-2024 |

### Current Blocker
The `swift-matter-examples` Swift code calls `esp_matter.endpoint.get_device_type_ids()` which was removed in a recent esp-matter commit. Need to either:
1. Check out esp-matter at the commit just before `9d7ff306` (the commit that removed the function) with matching submodules
2. Or find the exact esp-matter release tag that the swift-matter-examples repo was written against

Relevant git commits in esp-matter for `get_device_type_ids`:
- `18c5d4a5` — added
- `9d7ff306` — removed (most recent change)
- Target checkout: `9d7ff306~1`

---

## Next Steps

### Hardware
- [ ] Source compact buck converter (MP1584 or LM2596-based, 12V→5V, ≥1A) for final installation
- [ ] Wire K2 button shunt (second MOSFET) for RGB LED control (Phase 2)

### Firmware — Phase 1 (complete ✅)
- [x] Create humidifier project in `Matter-Humidifier/`
- [x] Implement Fan device type with FanControl cluster
- [x] Wire attribute callbacks to K1 MOSFET GPIO output
- [x] Map Apple Home PercentSetting to hardware states
- [x] Detect physical K1 presses and sync back to Matter
- [x] Commission into Apple Home

### Firmware — Phase 2 (next)
- [ ] Add second MOSFET for K2 button simulation
- [ ] Add second Matter endpoint — OnOff light cluster for RGB LED control
- [ ] Map on/off commands to K2 presses
- [ ] Test RGB LED control via Apple Home

### Notes App → DEVLOG workflow
- Take notes on iPad in Apple Notes
- Copy relevant notes into this file on Mac
- Claude Code can reference this file for project context

---

## Key Decisions & Rationale

- **ESP32-C6 over ESP32-C3**: C6 has native Thread (802.15.4); C3 is WiFi-only. Thread is preferred for Matter in Apple Home ecosystem.
- **Apple Home only**: Multi-ecosystem testing is out of scope. Matter's write-once-works-everywhere promise means the implementation won't change for other platforms.
- **Hardware-first sequencing**: Map all voltages and control signals before writing firmware, to avoid surprises later.
- **No 5V rail in humidifier**: Must use buck converter for final installation. MB102 is bench-only.
- **Button shunting over VRK interception**: Simulating K1 button presses is cleaner and safer than intercepting the ultrasonic transducer control signal. The green board continues to own all hardware control.
- **MOSFET over relay for button shunting**: Relay module is too bulky and noisy for button simulation. N-channel MOSFET (BS170/2N7000) briefly shorts K1 terminals cleanly.
- **Direct GPIO rejected**: Loading effect from GPIO input or multimeter probe disrupts the green board's button detection circuit. MOSFET isolation required.
- **Fan device type (0x0044)**: No native humidifier device type exists in Matter 1.4. Fan is the industry standard workaround (used by SwitchBot, TCL, others).
- **FanModeSequence 0x01**: Required to show discrete mode selector in Apple Home. Value 0x00 shows only a continuous speed slider.
- **Embedded Swift**: Application-level code written in Swift over ESP Matter C++ SDK. Toolchain is cutting edge (nightly Swift trunk snapshot required, not Xcode Swift).
