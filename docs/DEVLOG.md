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

---

## Session Log

### 2026-05-30 — New humidifier unit, grounding gotcha, Switch cluster shelved

**Hardware change**

Swapped to a newer humidifier variant. Front-panel labels are now **S1 MIST** and **S2 LIGHT** (replacing K1/K2 from the original unit). Functionally identical — both are momentary buttons that cycle modes — so the existing `fanButton` (S1) / `lampButton` (S2) variable naming still applies. Hardware mapping:

| Panel | Was | Now in code |
|---|---|---|
| S1 MIST | K2 (fan) | `fanButton` GPIO 2 shunt, `fanListenGPIO` 21 listen |
| S2 LIGHT | K1 (lamp) | `lampButton` GPIO 0 shunt, `lampListenGPIO` 1 listen |

**Mode cycle expanded to 5 states**

The new unit's MIST button cycles through five states instead of four: Off → On → 1H → 3H → 6H → Off (where 1H/3H/6H are timer-mode positions, not fan speeds). `modeForHardwareState` in `main/Main.swift` and the `sHumidifierModes[5]` table in `Matter/MatterInterface.cpp` are sized accordingly.

**Mode Select cluster (0x0050) added alongside Fan**

The Fan Control cluster (0x0202) doesn't have enough enum slots to express five modes cleanly, so a Mode Select endpoint (device type 0x0027) was added as an alternative surface for Apple Home. Implementation lives in:

- `Matter/MatterInterface.cpp` — `HumidifierModesManager` (a `SupportedModesManager` subclass with hardcoded modes) and `create_humidifier_mode_select_endpoint()`.
- `Matter/Clusters.swift` / `Matter/Attribute.swift` — `ModeSelect` cluster type and `CurrentMode` / `SupportedModes` / `Description` attribute structs.
- `Matter/Node.swift` — `MatterModeSelect` endpoint struct.
- `Matter/Matter.swift` — `Matter.ModeSelector` Swift class (renamed from `Matter.ModeSelect` to avoid shadowing the cluster type).

Current state: Mode Select is the active control surface in Main.swift; the Fan endpoint creation is commented out for now.

**Generic Switch cluster (0x003B) experiment — shelved**

Added a Generic Switch endpoint (device type 0x000F) with the MomentarySwitch feature bit, intended to expose the S2 LIGHT button as an automation trigger in Apple Home. Crashed at runtime when the button fired (`Data received on an unknown session` + stack dumps), and Apple Home reported "unconfigured button." Code is committed but commented out in `main/Main.swift`:

```swift
// let switchEndpoint = Matter.Switch(node: rootNode)
// ...
// rootNode.addEndpoint(switchEndpoint)
// switchEndpoint.press()
```

Likely fix when revisiting: calls to `SwitchServer::Instance().OnInitialPress(...)` need to hold the chip stack lock (`PlatformMgr().LockChipStack()` / `UnlockChipStack()`), and the `CurrentPosition` 1→0 write probably needs a small delay between transitions.

**Spurious-press infinite loop — root caused to floating ground**

Symptom: at boot the MIST LED cycled rapidly through states and the Matter console flooded with `CurrentMode received` events. Initial hypothesis was a code-side feedback loop — `fanButton.press()` was being called both in the physical-detection branch in the main loop *and* in the Mode Select event handler (with a `targetHw` vs `targetMode` variable-shadowing bug that made `presses` always non-zero on self-writes).

Both code-side issues were fixed:

1. `fanButton.press()` and `lampButton.press()` in the physical-detection branches of the main loop are commented out (they're redundant — the user already pressed the button physically; there's nothing to actuate).
2. `let presses = (Int(targetHw) - Int(hwState) + 5) % 5` in the Mode Select event handler corrected to `Int(targetMode)`. With this, self-writes (firmware syncing physical state back to Matter) compute `presses=0` and don't re-trigger the press loop.

But the loop continued — at a slower rate. Disconnecting the humidifier from the ESP entirely stopped the loop, confirming the K2 line itself was driving spurious negedge interrupts. **Root cause: missing common ground between XIAO and the humidifier's green board.** With grounds floating relative to each other, the listen GPIOs saw constant noise crossings of the negedge threshold. Tying GNDs together eliminated the loop.

**Open question after grounding fix**

After tying grounds, the loop stopped — but physical panel presses are no longer detected either. Two possibilities to investigate with a meter:

- K2/K1 lines *do* pulse LOW on panel press but GPIO 21/1 isn't seeing the negedge (wiring or pin-config issue).
- K2/K1 lines *don't* pulse LOW on panel press (the new green board may route the buttons through its MCU's matrix scan rather than as simple short-to-GND switches). If so, the listen-GPIO approach won't work and physical-press detection requires tapping a different node (e.g., the LED driver output line that changes when the mode cycles).

**Filed for next session.**

**Other small things**

- All debug `print()` calls in `main/Main.swift` and `main/ButtonShunt.swift` were prefixed with `[HUMI]` for easier console filtering (`idf.py monitor | grep '\[HUMI\]'`).
- Fixed a longstanding typo: `MatterConreteEndpoint` → `MatterConcreteEndpoint` across `Matter/Node.swift` (5 sites). The entry in `REDUNDANCIES.md` tracking this typo was removed.
- Investigated adding `acceptedCommandList` (global attribute 0xFFF9) but determined it's unnecessary — read-only, auto-populated by esp-matter, only useful if we wanted to decode the read events explicitly.

### 2026-05-31 — ButtonShunt polarity inverted for MOSFET gate drive

`main/ButtonShunt.swift` now drives the GPIO **HIGH to press, LOW to release** (was the opposite). Idle level at init is 0; `press()` raises the line for `durationMs` then lowers it again.

**Why:** the GPIO no longer connects directly to the button contacts — it drives an N-channel MOSFET gate. With the MOSFET in series, the polarity sense is reversed: HIGH on the gate turns the FET on and shorts the button (press), LOW turns it off (released). The original code's idle-HIGH / press-LOW would have left the FET conducting at all times, holding the button perpetually pressed.

No call-site changes were needed — the public API of `ButtonShunt` is unchanged. The header comment in the file was updated to describe MOSFET-gate semantics instead of the older direct-connect/open-drain rationale.

### 2026-06-02 — Data-model naming cleanup + Air Purifier endpoint scaffolded

**Endpoint facades renamed to Matter spec device-type names**

Audited the three layers of the data model (clusters in `Matter/Clusters.swift`, low-level endpoint pointer wrappers in `Matter/Node.swift`, high-level endpoint facades in `Matter/Matter.swift`) and confirmed the structure already conforms to the Matter spec — clusters are clusters, endpoints are endpoints. The confusion was purely naming. Two endpoint facades used names that collided (or threatened to collide) with Matter cluster names.

Renamed to disambiguate, using the Matter spec's device-type names:

| Before | After | Reason |
|---|---|---|
| `Matter.ModeSelector` | `Matter.ModeSelectDevice` | Frees the bare name; matches esp-matter's `mode_select_device` namespace |
| `Matter.Switch` | `Matter.GenericSwitch` | Matches spec device type "Generic Switch"; frees `Switch` for a future cluster wrapper for the Switch cluster (0x003B) |

`Matter.OnOffLight` and `Matter.Fan` were left unchanged — both already match the spec's device-type names cleanly (the matching clusters are named `OnOff` and `FanControl`, no collision).

Touched 5 sites across `Matter/Matter.swift`, `main/Main.swift`, and `main/borrador.swift`.

**Air Purifier endpoint (device type 0x002D) added**

Scaffolded a new endpoint type so the humidifier can be advertised to Apple Home as an Air Purifier instead of a Fan. The Air Purifier device type uses the same FanControl cluster as the Fan device type — only the device-type ID and the Apple Home rendering differ.

New code:

- `Matter/MatterInterface.cpp` — `create_humidifier_air_purifier_endpoint()` calling `esp_matter::endpoint::air_purifier::create()`.
- `Matter/MatterInterface.h` — prototype declaration.
- `Matter/Node.swift` — `struct MatterAirPurifier: MatterConcreteEndpoint` with `deviceTypeId = 0x002D`.
- `Matter/Matter.swift` — `class Matter.AirPurifier: Endpoint`. Exposes `updateFanMode()` and reuses the existing `matter_fan_update_mode` C shim (both endpoint types operate on the FanControl cluster).
- `main/Main.swift` — commented-out `airPurifierEndpoint` block parallel to the existing Fan one.

Filter monitoring clusters (HepaFilter 0x0071, ActivatedCarbon 0x0072) are not added — they're spec-optional and not relevant for a humidifier.

**Caveats when activating Air Purifier**

- Apple Home support for the Air Purifier device type landed in iOS 18. Test devices on earlier iOS may render as a fallback (Fan or generic).
- FanControl's FanMode enum maxes out at four functional values (Off/Low/Med/High plus On/Auto). Our 5-state hardware (Off/On/1H/3H/6H) still doesn't map cleanly — the placeholder handler in the commented Air Purifier block carries the same mapping bug we worked around in the Mode Select / OnOff iterations.

**Side topic: physical-press detection still unsolved**

Discussed the wiring options after last session's discovery that the K1/K2 lines may not pulse LOW on panel presses (new green board may use matrix scan). Advised:

1. Probe each candidate line with a meter during a panel press before re-wiring.
2. The most promising tap point is a mode-indicator LED drive line — state-change is unambiguous per mode, and LEDs are easy to identify on the green board.
3. When tapping, use a high-impedance divider (100kΩ/10kΩ) plus a small filter cap (100nF to GND) and consider a Schmitt-trigger buffer or optocoupler for clean digital edges and isolation.

No code changes from this part — user is probing.

**Gotcha: commissioning BLE is range-sensitive**

Spent ~30 minutes chasing a "Connecting…" hang in the Apple Home pairing flow. Symptoms:

- BLE pairing reaches PASE completion successfully.
- Then ~60s of complete silence on the log, followed by `chip[FS]: Fail-safe timer expired` and `Commissioning failed (attempt 1): 32` (CHIP_ERROR_TIMEOUT).
- Subsequent retries fail even earlier with `PASESession timed out` and `BLE GAP connection terminated (con 0 reason 0x208)` — HCI reason 0x08 = connection timeout.

Initial hypotheses chased and ruled out: network credentials, Thread Border Router presence, sdkconfig transport selection. Actual root cause: **the BLE link itself was dropping mid-handshake.** Network commissioning never started because BLE died before WiFi/Thread credentials could be delivered over the same BLE channel.

**Fix:** move the ESP32-C6 within ~1 m of the iPhone, line of sight, before tapping "Add Accessory." Commissioning then completes normally and the device transitions to Thread/WiFi operational mode where range stops mattering.

**For future debugging:** if you see PASE completing followed by 60s of silence and a fail-safe expiry, suspect BLE range/interference before chasing certificates or network setup. The C6's onboard antenna is small and 2.4 GHz BLE during commissioning is more sensitive than the same chip's operational radio (which uses 802.15.4 / Thread at the same frequency band but with much more aggressive retransmission).

**Air Purifier endpoint activated with stacked clusters (FanControl + OnOff + ModeSelect)**

After commissioning was working, switched the mist control from the OnOffLight kludge over to a real Air Purifier endpoint with multiple clusters on one endpoint — illustrating Matter's "endpoint contains clusters" hierarchy.

**Design:** the existing `Matter.AirPurifier` (device type 0x002D) was extended so the endpoint now carries:

- Identify + Groups (mandatory globals, from `air_purifier::create()`)
- **FanControl** (mandatory for device type 0x002D, from `air_purifier::create()`)
- **OnOff** (added manually, no Lighting feature flag)
- **ModeSelect** (added manually, reusing the existing `sHumidifierModesManager` delegate from the standalone ModeSelect endpoint)

**Implementation notes:**

- `create_humidifier_air_purifier_endpoint()` in `Matter/MatterInterface.cpp` had to be moved past the anonymous namespace holding `sHumidifierModesManager` since the factory now references it.
- Cluster additions use the lower-level `esp_matter::cluster::on_off::create()` and `esp_matter::cluster::mode_select::create()` rather than any endpoint-level factory. Flags qualified with `esp_matter::CLUSTER_FLAG_SERVER` (not bare `CLUSTER_FLAG_SERVER`) — that namespace isn't pulled in via `using` at the top of the file.
- `Matter.AirPurifier` Swift facade gained `update(on:)` and `updateCurrentMode(_:)` alongside the existing `updateFanMode(_:)`. All three reuse the corresponding `matter_*_update_*` C shims unchanged — those shims are keyed by `(endpoint_id, cluster_id, attribute_id)`, so they work on any endpoint that hosts the right cluster.
- The OnOffLight kludge in `main/Main.swift` is commented out; the new `mistEndpoint = Matter.AirPurifier(...)` uses a `switch` over `event.attribute` to dispatch OnOff vs. CurrentMode vs. FanControl writes to different press-logic branches.

**Open issues with the stacked-cluster design (not yet resolved):**

1. **Three views of the same state.** OnOff, CurrentMode, and FanMode are all writable controls expressing variations of "what should the mist do." If Apple Home writes to one, we react with a button press; the other two clusters' attribute values then need to be reconciled so subsequent reads don't disagree. Currently only the path that fired the press updates its tracking variable. The `mistIsOn = (hwState != 0)` line in the CurrentMode branch covers one of the three pairwise sync gaps.
2. **Apple Home rendering.** Air Purifier's primary tile in Home centers FanControl; OnOff and ModeSelect attributes likely surface only in Settings → Accessory Details. Worth confirming on a fresh re-commission whether Home shows the additional controls at all.
3. **FanControl writes aren't actuated.** The placeholder handler logs FanControl events but doesn't press the button. If you start using the FanControl tile in Home, you'll need to either implement the mode-to-state mapping or route FanControl writes through the same press logic as ModeSelect.
4. **Self-press suppression still missing.** A Home write to ModeSelect that calls `fanButton.press()` N times triggers N listen ISRs (when the K2 line is properly grounded), which the main loop's physical-press branch will count as N user presses and bump `hwState` accordingly — double-counting. The earlier "ignoreNext counter" approach is the right fix when you revisit.

### 2026-06-03 — Panel-LED sensing pivot; new panel cable; pin assignments locked

**Hardware change: new humidifier unit with JST PH002 7-pin panel cable**

The test unit was replaced. The new unit ships a 7-wire flat cable (JST PH002) that connects the front-panel PCB to the control board. All sensing and shunting now routes through this cable. A debug splice cable was made that taps all 7 lines without breaking the circuit.

Panel cable map:

| Pin | Signal | Notes |
|---|---|---|
| L1 | LIGHT (S2) button line | Idle ~5 V via panel pull-up; momentarily 0 V on press |
| L2 | MIST row selector (analog) | 0 V = off, ~2.2 V = modes 3H/6H, ~3.1 V = modes On/1H |
| L3 | MIST col A (digital) | LOW when mode is On or 3H |
| L4 | MIST col B (digital) | LOW when mode is 1H or 6H |
| L5 | GND | Must tie to XIAO GND — see "common ground" note |
| L6 | +5 V panel power | Optional XIAO power source |
| L7 | (unused) | ~55 mV @ 50 kHz noise from SMPS; no useful signal |

**MIST state detection pivoted from button-listen ISR to panel-LED sensing**

The K1/K2 button-line ISR approach failed on the new board. Physical button presses do not produce clean LOW pulses on the button lines accessible from the panel cable — the new control board's MCU scans the matrix internally and the line transitions are either absent or too noisy for edge detection.

New approach: read three lines from the panel cable every main-loop tick and decode the active LED state. The panel uses a 2×2 matrix encoding:

- **L2 (row):** analog voltage selects which LED pair is active
- **L3/L4 (columns):** digital levels select which LED within the pair is active

Decoding table (after 100kΩ/100kΩ divider halves all voltages):

| mv range | col_a (L3 LOW?) | col_b (L4 LOW?) | hwState |
|---|---|---|---|
| < 200 | — | — | 0 (Off) |
| > 1300 | yes | no | 1 (On) |
| > 1300 | no | yes | 2 (1H) |
| 200–1300 | yes | no | 3 (3H) |
| 200–1300 | no | yes | 4 (6H) |

Key guard: `exactly_one_col_active = (col_a_lo != col_b_lo)`. If both columns are LOW or both are HIGH, the read is treated as Off regardless of L2 — this rejects the panel's ~10 s LED refresh blip that briefly lifts L2 without changing the column lines.

**ADC + calibration**

`setup_mist_panel_sensors()` in `Matter/MatterInterface.cpp` initialises the ESP-IDF `adc_oneshot` API on ADC1 channel 1 (GPIO1/D1) with 12 dB attenuation (0–3.3 V usable range) and curve-fitting calibration. The read function averages 8 samples per poll to reject single-sample noise.

**Pin assignments — final (confirmed from Seeed datasheet)**

XIAO ESP32-C6 silkscreen D-numbers do **not** generally map 1:1 to GPIO numbers past D2. Confirmed mapping used in firmware:

| XIAO pin | GPIO | Function |
|---|---|---|
| D0 | 0 | LIGHT MOSFET shunt OUTPUT |
| D1 | 1 | L2 MIST row ADC (ADC1_CH1) |
| D2 | 2 | MIST MOSFET shunt OUTPUT |
| D7 | 17 | L1 LIGHT button ISR input |
| D10 | 18 | L3 MIST col A digital input |
| D5 | 23 | L4 MIST col B digital input |
| GND | — | L5 (common ground — mandatory) |

**All sensor lines need voltage dividers** (panel outputs up to ~5 V; GPIO max 3.6 V). Standard circuit per line: 100 kΩ series + 100 kΩ to GND + optional 100 nF filter cap.

**Common ground is non-negotiable.** Without tying XIAO GND to L5, all listen GPIOs float relative to the panel and produce constant spurious ISR fires. This burned two separate debugging sessions — see grounding note in memory files.

**Debounce added to MIST sensing**

`Main.swift` main loop wraps `matter_read_mist_state()` in a 2-consecutive-read debounce (≈ 400 ms at 200 ms tick). A new state is only committed to `hwState` and propagated to Matter once the same value is read twice in a row. This filters the panel's empty-tank transient: when the tank runs dry, the hardware briefly flashes to mode-1 voltage on L4 before cutting off — without debounce this registers as a spurious On→Off→On transition.

**Watchdog trip root-caused: `delay_ms(5)` = 0 FreeRTOS ticks**

At default 100 Hz FreeRTOS tick rate (10 ms/tick), `delay_ms(5)` calls `vTaskDelay(pdMS_TO_TICKS(5))` which rounds down to 0 ticks — a no-op. The main loop becomes a busy-spin, starving the Matter background task and tripping the watchdog. Fixed by using `delay_ms(200)` for the main polling tick.

**ButtonShunt MOSFET polarity note**

Gate HIGH → MOSFET conducts → button shorted (press). Gate LOW → released. Init sets GPIO LOW (idle released state).

---

### 2026-06-04 — AirPurifier endpoint active; stacked-cluster design; watchdog and loop debugging

**Endpoint configuration**

Activated the `Matter.AirPurifier` endpoint (device type 0x002D) with three clusters stacked on one endpoint: FanControl (mandatory), OnOff (added manually), and ModeSelect (added manually, reusing `sHumidifierModesManager`). Apple Home renders an air-purifier tile. ModeSelect `StandardNamespace` was set to 0x0040 (Common Mode Namespace) to try to make Home render the custom mode list — it reads the namespace correctly in Matter logs but still does not surface SupportedModes in any Home UI element.

**`nullable<uint16_t>` usage clarification**

`nullable<T>` in esp-matter is a global-namespace class, not `chip::app::DataModel::Nullable`. Constructor form: `nullable<uint16_t>(ns)`. Used in `set_mode_select_standard_namespace()` helper.

**Compile fix: `CLUSTER_FLAG_SERVER` scope**

Must qualify as `esp_matter::CLUSTER_FLAG_SERVER` — not visible as a bare name despite `using namespace esp_matter` elsewhere in the file.

**Feedback loop after hardware reconnect**

On reconnecting the humidifier, the Matter console logged a non-stop sequence alternating between `mist state change 3 → 4` and `4 → 3`. Isolated to a **loose wire on L4** (MIST col B) — L4 was intermittently floating, making col_b_lo toggle randomly and causing the state decoder to oscillate between two valid states. Reseating the L4 wire on the breadboard resolved it entirely.

---

### 2026-06-05 — ModeSelect UI investigation; OnOff kludge reinstated; code committed

**ModeSelect cluster never renders SupportedModes in Apple Home**

Two attempts:

1. Standalone `ModeSelectDevice` endpoint (device type 0x0027) with `HumidifierModesManager` delegate — Home reads `FeatureMap`, `ClusterRevision`, `Description`, `CurrentMode` but never reads `SupportedModes`.
2. ModeSelect cluster added to the AirPurifier endpoint — same result; Home renders the air-purifier FanControl tile but the extra cluster is invisible.

Matter chip-tool confirmed the cluster and its modes are present and readable at the protocol level. The limitation appears to be Apple Home's own rendering policy: it does not surface third-party custom mode lists regardless of `StandardNamespace` value tested (0x0000, 0x0040). This is an Apple Home ecosystem limitation, not a firmware bug.

**Decision: revert to OnOffLight kludge for MIST control surface**

Two `Matter.OnOffLight` endpoints are now the active configuration:
- `mistEndpoint`: MIST on/off, device type 0x0100 (renders as a round light icon in Home)
- `lightEndpoint`: LIGHT/lamp on/off, same device type

The AirPurifier, FanControl, ModeSelectDevice, and GenericSwitch code paths are all preserved as commented-out blocks in `main/Main.swift` and `Matter/MatterInterface.cpp` for future revival.

`rootNode.addEndpoint(mistEndpoint)` and `rootNode.addEndpoint(lightEndpoint)` are the only active endpoint additions.

**Commission and verify**

Device repaired with Apple Home after the kludge change. Both round-button tiles appear and correctly drive the respective MOSFET shunts when toggled.

**Commit: "Final version with no LIGHT button detection"**

Staged and pushed:
- `Matter/Matter.swift` — AirPurifier, ModeSelectDevice, GenericSwitch facades (scaffolded, inactive)
- `Matter/MatterInterface.cpp` — ADC+panel sensing, stacked-cluster factory, ModeSelect namespace helper
- `Matter/MatterInterface.h` — all shim prototypes
- `Matter/Node.swift` — MatterAirPurifier struct, MatterConcreteEndpoint typo fix
- `main/ButtonShunt.swift` — final pin constants
- `main/Main.swift` — LED-state polling, OnOff kludge active, alternatives commented out
- `Matter-Humidifier/REDUNDANCIES.md`, `docs/DEVLOG.md`, `docs/WIRING-UPDATE.md`, `CLAUDE.md`

---

### 2026-06-06 — LIGHT button detection saga (full arc)

This session was entirely focused on trying to detect physical presses of the LIGHT (S2) panel button so that Apple Home reflects presses made directly on the device. The saga covers five distinct approaches and ends with a fundamental electrical conclusion.

**Goal:** When someone presses S2 LIGHT on the humidifier panel, flip `lampIsOn` and call `lightEndpoint.update()` so Apple Home stays in sync.

**Approach 1: Negedge ISR on L1 (initial, after voltage divider)**

L1 carries the LIGHT button line. The panel has an internal pull-up to ~5 V; button press shorts to GND. After the 100 kΩ/100 kΩ divider, expected idle ~2.5 V, pressed ~0 V → clean negedge.

Result: ISR fired continuously even at idle. Scope showed L1 had strong ~100 Hz / 0.4 V spikes superimposed on idle voltage. The panel's internal active pull-down (~1 kΩ) was dominating — the panel always drives L1 actively, so the divider idled far closer to 0 than expected. The spikes alone were enough to cross the negedge threshold repeatedly.

**Approach 2: Polling with sliding window**

Replaced ISR with a polling loop that measured GPIO level 40 times per 200 ms tick and classified a "press" only if ≥ 50% of samples were HIGH. Rationale: if pressed = LOW and idle = HIGH, the distribution should differ.

Result: L1 measured mostly LOW in both idle AND pressed states (the panel's active pull-down dominates in both conditions). The rare HIGH samples were noise crossings from the 100 Hz EMI. No reliable discrimination. Abandoned.

**Approach 3: Remove voltage divider; direct connection with internal pull-up**

Bypassed the divider. Enabled ESP32-C6 internal pull-up (~45 kΩ). With the panel's ~1 kΩ active pull-down now fighting a 45 kΩ pull-up, idle voltage ≈ 1 kΩ/(45kΩ+1kΩ) × 3.3 V ≈ 0.07 V — nowhere near HIGH. ISR trigger impossible.

Result: `LIGHT button press detected` fired in a continuous loop immediately. The internal pull-up was too weak to pull L1 anywhere near the HIGH threshold against the panel's 1 kΩ pull-down.

**Approach 4: External 1 kΩ pull-up to 3.3 V; internal pull-up disabled; ISR reinstated**

External 1 kΩ pull-up from L1 to XIAO 3.3 V rail. This creates a voltage divider with the panel's ~1 kΩ internal pull-down:

- Idle: 1 kΩ ext / (1 kΩ ext + 1 kΩ panel) × 3.3 V ≈ 1.65 V
- Pressed: button adds ~6 kΩ parallel path → effective panel impedance ≈ 0.857 kΩ → voltage ≈ 1.53 V

Both states sit right at the ESP32-C6 Schmitt trigger threshold (~1.4 V typ). The 120 mV differential is not enough for reliable discrimination. ISR still fired spuriously, and probing with a DMM in AUTO mode triggered additional false presses (meter's own probe capacitance was enough to cross the threshold).

**Root-cause measurement:**

User measured L1 resistance:
- Idle: ~1 kΩ to GND (the panel's active pull-down)
- Pressed: ~0.86 kΩ to GND (button adds ~6 kΩ parallel path → 1 kΩ || 6 kΩ ≈ 0.86 kΩ)

The button itself is not a short to GND — it adds a ~6 kΩ resistive path in parallel with the panel's existing ~1 kΩ pull-down. Net resistance change is only 14%. There is no operating point where L1 is unambiguously HIGH at idle and LOW when pressed.

**Conclusion**

L1 detection with a digital GPIO is fundamentally unreliable on this hardware without additional external components. Options considered:

- **(A)** Tap the lamp drive line inside the unit (LED cathode or transistor collector) — would give a clean digital signal, but requires opening the unit and probing the green PCB, which is more invasive than originally scoped.
- **(B)** Use ADC sampling on L1 with statistical thresholding — marginal; 120 mV difference over a noisy rail is unlikely to be reliable long-term.
- **(C)** Accept one-way control (Home → device only; physical presses don't sync to Home).

**Decision: accept one-way control (option C) and move on.**

Physical presses of S2 LIGHT will not be detected or reflected in Apple Home. Apple Home → lampButton MOSFET shunt → humidifier works correctly and remains the primary control path. The L1 ISR code is still registered in firmware but fires spuriously — a final cleanup (comment out `setup_lamp_button_listen_gpio` call) was left as a pending task (see "Known Issues / Pending Work" below).

**Scope / EMI note for the record**

The ~100 Hz / 0.4 V spikes on L1 correlate with the panel's own LED scan rate. The panel cycles its LED matrix at ~100 Hz to time-multiplex the LEDs; each scan pulse capacitively couples into L1 through parasitic paths on the green PCB. This is why the spikes persist even with no button press and why turning MIST on (more LEDs active) increases the spike rate and amplitude. An optocoupler or a dedicated comparator with hysteresis (e.g., LM393 with a reference between 1.53 V and 1.65 V) could discriminate L1 reliably, but neither is in the current parts inventory.

---

### 2026-06-09 — Session wrap-up; machine migration prep

**Current firmware state (as of this entry)**

Active configuration in `main/Main.swift`:
- `mistEndpoint` = `Matter.OnOffLight` — MIST on/off control (OnOff cluster, device type 0x0100)
- `lightEndpoint` = `Matter.OnOffLight` — LIGHT/lamp on/off control
- Both render as round light icons in Apple Home
- MIST state is read back from panel-LED sensing (L2/L3/L4 ADC+digital) and synced to `mistEndpoint` every ~200 ms
- LIGHT presses on the physical panel do NOT sync to Apple Home (one-way control — see LIGHT button saga above)
- `setup_lamp_button_listen_gpio(lightButtonInputGPIO)` is still called in `main()` and will fire spuriously — pending comment-out

MIST hardware state cycle: Off(0) → On(1) → 1H(2) → 3H(3) → 6H(4) → Off(0)

**Known issues / pending work**

1. **LIGHT ISR spurious fires:** `setup_lamp_button_listen_gpio(lightButtonInputGPIO)` at `Main.swift:53` should be commented out. With the 1 kΩ external pull-up, L1 idles at ~1.65 V (at Schmitt threshold) and will trigger negedge ISRs from ambient noise. Since LIGHT detection is accepted as one-way, there is no benefit to registering the ISR. Remove the call on the next code change.

2. **`mistIsOn` drift vs actual hardware state:** `mistIsOn` is a bool tracking whether the mist is "on" for the OnOffLight cluster. `hwState` tracks the 5-state hardware position (0–4). After physical button presses cycle through On/1H/3H/6H and back to Off, the two can get out of sync because `mistIsOn` is updated whenever `hwState` transitions between 0 and non-0, but the OnOff cluster only has two values. Currently acceptable for the kludge endpoint — if the ModeSelectDevice or AirPurifier endpoint is ever activated, this drift disappears.

3. **Self-press suppression for MIST:** When Apple Home calls `fanButton.press()` via `mistEndpoint.eventHandler`, the MOSFET on D2/GPIO2 briefly pulls the S1 line LOW. This capacitively couples into L2 (the MIST row ADC line on D1/GPIO1), which can spike the ADC reading and register a spurious state change in `matter_read_mist_state()`. A `fanPressesPending` counter that suppresses one state-change report after each Home-initiated press was proposed but never implemented.

4. **Boot-time false MIST read:** On occasional boots, `matter_read_mist_state()` returns 1 on the first few polls even when the humidifier is off. Likely caused by the panel's boot sequence briefly lighting LEDs. The 2-read debounce prevents this from propagating to Matter, but the first read still sets `pendingMistState = 1` until the device stabilizes. Minor; acceptable.

5. **Duplicate `return` in `create_humidifier_fan_endpoint()`:** `MatterInterface.cpp:49` has an unreachable second `return esp_matter::endpoint::fan::create(...)`. Pre-existing bug; the function returns on line 48 so line 49 is never reached. Remove on next touch.

**Machine migration checklist**

To resume development on a new machine:

1. **Clone the repo:**
   ```
   git clone <repo-url> iot-projects
   cd iot-projects
   ```

2. **Install ESP-IDF v5.4.1:**
   - Follow Espressif's standard installer for macOS. Target version: `v5.4.1`.
   - After install, add an alias to `~/.zshrc`:
     ```
     alias get_esp541='. <ESP_IDF_PATH>/export.sh && . <ESP_MATTER_PATH>/export.sh'
     ```

3. **Install esp-matter release/v1.4:**
   - Clone `esp-matter` at tag/branch `release/v1.4`.
   - Run `install.sh`.
   - Initialize submodules with `--platform esp32 darwin --shallow` (full submodule checkout is very large).
   - Set `ESP_MATTER_PATH` in shell config.

4. **Install nightly Swift trunk toolchain:**
   - Download from https://www.swift.org/download → "Trunk Development (main) snapshot"
   - Install to `/Library/Developer/Toolchains/`
   - Verify: `plutil -extract CFBundleIdentifier raw /Library/Developer/Toolchains/swift-latest.xctoolchain/Info.plist`
   - The toolchain identifier must be exported as `TOOLCHAINS` before building.

5. **Build:**
   ```bash
   # Fresh terminal every time
   get_esp541
   cd iot-projects/Matter-Humidifier
   export TOOLCHAINS=$(plutil -extract CFBundleIdentifier raw /Library/Developer/Toolchains/swift-latest.xctoolchain/Info.plist)
   idf.py set-target esp32c6
   idf.py build
   ```

6. **Flash and monitor:**
   ```bash
   idf.py erase-flash flash monitor
   ```

7. **Wiring:** reproduce the breadboard from `docs/WIRING-UPDATE.md`. Key checklist:
   - L5 (panel GND) → XIAO GND — **mandatory before powering on**
   - L2 → 100kΩ/100kΩ divider → D1/GPIO1 (ADC)
   - L3 → 100kΩ/100kΩ divider → D10/GPIO18
   - L4 → 100kΩ/100kΩ divider → D5/GPIO23
   - L1 → external 1kΩ pull-up to 3.3V → D7/GPIO17 (ISR input, currently unreliable — see LIGHT button saga)
   - MIST MOSFET: D2/GPIO2 → gate; drain/source across S1 contacts
   - LIGHT MOSFET: D0/GPIO0 → gate; drain/source across S2 contacts

8. **Commissioning:**
   - Erase flash before first commission (`idf.py erase-flash`).
   - Keep iPhone within ~1 m of XIAO during BLE pairing.
   - Passcode: `20202021`.
   - If pairing stalls silently after the device transmits its DAC certificate, **unplug the HomePod mini / Apple TV 4K for ~2 minutes**, let it re-establish, then retry — the Hub caches attestation rejections.

9. **Verify:**
   - Apple Home shows two round light icons (MIST and LIGHT).
   - Toggling MIST tile on/off causes MOSFET shunt to pulse; humidifier LED changes state.
   - Toggling LIGHT tile causes LIGHT MOSFET to pulse; humidifier lamp changes state.
   - Physical MIST button presses on the panel are reflected in Home within ~400 ms.
   - Physical LIGHT button presses are NOT reflected in Home (one-way control — expected).
