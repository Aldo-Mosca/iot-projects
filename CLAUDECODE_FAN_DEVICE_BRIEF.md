# Task Brief: Matter Fan Device Type Implementation
## For Claude Code — Matter-Humidifier Project

---

## Context

We are building a Matter accessory in Embedded Swift on a Seeed Studio XIAO
ESP32-C6 that controls a consumer room humidifier via button shunting (GPIO
briefly pulled low to simulate K1 button press on the humidifier's green
control board).

The project is based on `swift-matter-examples` (Apple/swiftlang WWDC24),
adapted to run against **esp-matter release/v1.4** and **ESP-IDF v5.4.1**.

The starting point for this work is the `smart-light` example from
`swift-matter-examples`, copied into the `Matter-Humidifier/` project directory.
The C++ shim architecture is preserved: `MatterInterface.h` / `MatterInterface.cpp`
are thin wrappers around the esp-matter C++ API, and `Matter.swift` / `Node.swift`
are the Swift abstraction layer on top.

---

## Humidifier Hardware State Machine

The humidifier cycles through four states on each K1 button press:

```
Off (0) → High (1) → Low (2) → Night (3) → Off (0) → ...
```

- **Off**: fan stopped, LED off
- **High**: high fan speed, RGB LED on
- **Low**: low fan speed, RGB LED on  
- **Night**: low fan speed, LED off

The XIAO controls this by:
1. **Simulating presses**: briefly pulling a GPIO low to shunt the K1 button
   contacts (the green board's MCU sees this as a normal button press)
2. **Detecting physical presses**: listening on a second GPIO wired to the K1
   button line as a passive input interrupt

State is tracked in firmware as a counter (0–3). On boot, state is assumed Off.

---

## Matter Device Type Decision

**Device type: Fan (`0x0044`)**

Rationale: No native humidifier device type exists in Matter 1.4 or 1.5.
The industry workaround (used by SwitchBot, TCL) is the Fan device type.
Apple Home renders it with speed/mode controls, which maps well to the
four humidifier states.

**Do NOT use:**
- On/Off Plug-in Unit (`0x010A`) — loses mode visibility
- Dimmer Switch (`0x0104`) — this is a *controller* type, semantically wrong
- Mode Select (`0x0027`) — Apple Home support is inconsistent

---

## Required Clusters for Fan Device Type

The Fan device type (`0x0044`) requires these server-side clusters:

| Cluster | ID | Purpose |
|---|---|---|
| Identify | `0x0003` | Required by spec |
| Groups | `0x0004` | Required by spec |
| Fan Control | `0x0202` | Core cluster — carries mode and speed |

The **Fan Control cluster** (`0x0202`) is the key one. Its relevant attributes:

| Attribute | ID | Type | Our mapping |
|---|---|---|---|
| FanMode | `0x0000` | enum8 | Off=0, Low=1, Medium=2 (unused), High=3, On=4 (unused), Auto=5 (unused) |
| FanModeSequence | `0x0001` | enum8 | Set to `0x03` = Off/Low/Medium/High |
| PercentSetting | `0x0002` | uint8 nullable | Optional — can expose if useful |
| PercentCurrent | `0x0003` | uint8 | Optional |

**FanMode values we care about:**

| FanMode value | Label in Apple Home | Maps to humidifier state |
|---|---|---|
| 0 (Off) | Off | Off |
| 1 (Low) | Low | Low (low fan, LED on) |
| 3 (High) | High | High (high fan, LED on) |

Night mode is a complication — there is no direct FanMode equivalent.
**Use PercentSetting or a Wind Setting feature to encode Night**, or simply
map Night to Low in the Matter model and handle it as a sub-mode internally.
Discuss with the human before deciding — do not silently collapse Night into Low.

---

## What Needs to Change

### 1. `app_main.cpp` (or equivalent entry point)

Replace the Extended Color Light endpoint declaration with a Fan endpoint.

**Old pattern (smart-light):**
```cpp
extended_color_light::config_t light_config;
endpoint_t *endpoint = extended_color_light::create(node, &light_config,
    ENDPOINT_FLAG_NONE, NULL);
```

**New pattern (fan):**
```cpp
#include <esp_matter_endpoint.h>

fan::config_t fan_config;
// FanModeSequence: 0x03 = Off/Low/Medium/High
fan_config.fan_control.fan_mode_sequence = 3;
endpoint_t *endpoint = fan::create(node, &fan_config,
    ENDPOINT_FLAG_NONE, NULL);
```

Verify the exact config struct field names against the current
`esp_matter_endpoint.h` in esp-matter release/v1.4 before writing code.
The struct layout may differ from older versions.

### 2. `MatterInterface.h` / `MatterInterface.cpp`

The existing shim exposes an `ExtendedColorLight` abstraction. This needs
to be replaced or extended with a `Fan` abstraction.

The key shim functions needed:

```cpp
// Initialize a Fan endpoint and return its endpoint_id
uint16_t matter_fan_init(uint8_t initial_fan_mode);

// Called by the Matter stack when Apple Home changes the fan mode
// Registers a callback that Swift will implement
void matter_fan_set_mode_callback(void (*callback)(uint8_t fan_mode));

// Called by Swift/firmware when a physical button press changes the state
// Updates the Fan Control cluster attribute in the Matter data model
esp_err_t matter_fan_update_mode(uint16_t endpoint_id, uint8_t fan_mode);
```

**Critical:** `get_device_type_ids` was removed from esp-matter in a recent
commit and does NOT exist in release/v1.4. Do not call it. Device type is
determined at endpoint creation time via the `fan::create()` call — there
is no need to query it back afterward.

### 3. `Matter.swift` (Swift abstraction layer)

Replace `Matter.ExtendedColorLight` with `Matter.Fan`.

```swift
// Rough shape — adapt to match the existing Swift shim patterns in the repo
extension Matter {
    class Fan {
        var onModeChange: ((UInt8) -> Void)?
        private let endpointId: UInt16
        
        init(initialMode: UInt8 = 0) {
            // calls matter_fan_init() via C++ shim
        }
        
        func updateMode(_ mode: UInt8) {
            // calls matter_fan_update_mode() via C++ shim
            // called when a physical K1 press is detected
        }
    }
}
```

### 4. `Node.swift` (top-level Swift entry point)

Replace the `ExtendedColorLight` instantiation with `Fan`.
Wire the `onModeChange` callback to the GPIO output function that
simulates K1 button presses.

Wire the GPIO interrupt handler (physical K1 press detection) to call
`fan.updateMode()`.

### 5. `BridgingHeader.h`

Add the new shim function declarations so Swift can see them:

```c
uint16_t matter_fan_init(uint8_t initial_fan_mode);
void matter_fan_set_mode_callback(void (*callback)(uint8_t fan_mode));
esp_err_t matter_fan_update_mode(uint16_t endpoint_id, uint8_t fan_mode);
```

---

## Fan Mode → K1 Press Sequence Logic

When Apple Home requests a mode change, the firmware must calculate how many
K1 presses are needed to reach the target mode from the current mode, then
fire that many GPIO pulses with ~200ms between each.

```
presses_needed = (target_mode - current_mode + 4) % 4
```

Example: current=High(1), target=Night(3) → (3-1+4)%4 = 2 presses.

This logic belongs in Swift (in `Node.swift` or a `Humidifier` helper class),
not in the C++ shim. The shim just fires a single GPIO pulse when called.

---

## GPIO Pin Assignments (TBD)

GPIO pin numbers for K1 shunt output and K1 listen input are not yet
confirmed. Use `#define` or a Swift constant with a clear `TODO` comment.
Do not hardcode without marking it.

---

## Files To Touch

```
Matter-Humidifier/main/
├── app_main.cpp          ← replace light endpoint with fan endpoint
├── MatterInterface.h     ← replace light shim declarations with fan shim
├── MatterInterface.cpp   ← replace light shim implementation with fan shim
├── BridgingHeader.h      ← update C declarations visible to Swift
└── Matter/
    ├── Matter.swift      ← replace ExtendedColorLight with Fan class
    ├── Node.swift        ← wire fan callbacks and GPIO logic
    └── LED.swift         ← DELETE or stub out (no LED driver needed)
```

---

## Do Not Change

- `CMakeLists.txt` structure (unless a new source file is added)
- `sdkconfig.defaults` (Matter/WiFi/BLE settings are correct)
- `partitions.csv`
- The overall build system setup (idf.py, TOOLCHAINS env var, etc.)
- The Swift/C++ shim *architecture* — only the contents change

---

## Key Constraints

- **esp-matter release/v1.4** — API may differ from what online tutorials show.
  Always check actual header files in
  `$ESP_MATTER_PATH/components/esp_matter/data_model/esp_matter_endpoint.h`
  before writing code.
- **No `get_device_type_ids`** — this function does not exist in v1.4.
- **Embedded Swift** — no stdlib, no Foundation, no dynamic dispatch on ARC.
  Follow patterns already established in the existing Swift files exactly.
- **Fresh terminal + `get_esp541`** before any `idf.py` invocation.
- **`TOOLCHAINS` env var** must be set to the nightly Swift trunk toolchain,
  not Xcode Swift. Check with:
  `plutil -extract CFBundleIdentifier raw /Library/Developer/Toolchains/swift-latest.xctoolchain/Info.plist`

---

## Open Question (Discuss Before Implementing)

**Night mode mapping**: The humidifier's Night state (low fan, LED off) has
no direct equivalent in the Matter Fan Control cluster's FanMode enum.
Options:
1. Map Night → Low in Matter (lossy — Apple Home can't distinguish Night from Low)
2. Use `WindSetting` feature flags to encode Night as a special mode
3. Ignore Night from the Matter side; Night is only reachable via physical button

Ask the human which approach to take before writing the Night mode handling code.
