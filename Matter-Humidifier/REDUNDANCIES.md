# Matter-Humidifier — Code Redundancy & Inconsistency Audit

> Scope: source files under `main/` and `Matter/`.  
> Build artifacts, `managed_components/`, and `CMakeLists.txt` boilerplate are out of scope.  
> No code has been modified; this document is read-only analysis.

---

## A. Unreachable / Dead Code

### A1. Duplicate `return` statement — `Matter/MatterInterface.cpp:40-41`

```cpp
return esp_matter::endpoint::fan::create(node, &config, 0x00, priv_data);
return esp_matter::endpoint::fan::create(node, &config, 0x00, priv_data);  // ← unreachable
```

The second `return` on line 41 is identical to line 40 and can never execute.  
The function returns (and the endpoint is created) on line 40.

---

### A2. Commented-out duplicate in `matter_fan_update_mode` — `Matter/MatterInterface.cpp:69-71`

```cpp
// esp_matter_attr_val_t val = esp_matter_uint8(fan_mode);
// return esp_matter::attribute::update(endpoint_id, 0x00000202, 0x00000000, &val);
// Update FanMode (0x0000)
```

These three lines are the remnant of a simpler earlier version of the function.  
The live implementation below them supersedes them completely.

---

### A3. `LED.swift` — orphaned file, never compiled

`main/LED.swift` is a complete 89-line LED driver class but:

- It is **not listed** in `CMakeLists.txt` `target_sources`.
- It is **never instantiated** anywhere in `Main.swift`.
- The `led_driver_*` C functions it calls exist only because `BridgingHeader.h` pulls in `led_driver.h` (see A5).

---

### A4. `Relay.swift` — orphaned file, never compiled

`main/Relay.swift` is a 25-line relay driver class with the same issues:

- Not listed in `CMakeLists.txt` `target_sources`.
- Never instantiated in `Main.swift`.

---

### A5. `#include "led_driver.h"` in `BridgingHeader.h:49`

```c
#include "led_driver.h"   // ← must be here according to claude.ai
```

`LED.swift` (the only consumer of `led_driver_*` functions) is not compiled.  
This include is dead weight and may pull in unnecessary build dependencies.

---

### A6. `ColorControl` and `LevelControl` attribute cases in `Matter.swift:96-131`

`Matter.Endpoint.Attribute` handles `.colorControl` (with six sub-cases) and `.levelControl`.  
The current application creates only a `Fan` endpoint and an `OnOffLight` endpoint.  
Neither uses a ColorControl or LevelControl cluster, so these branches are never reached.

---

### A7. Commented-out `updateFanSpeed` — `Matter/Matter.swift:171-173`

```swift
// func updateFanSpeed(_ speed: UInt8) {
//   matter_fan_update_mode(UInt16(id), mode)
// }
```

Incomplete (references `mode` which is not in scope) and superseded by `updateFanMode`.

---

### A8. Commented-out debug blink block — `main/ButtonShunt.swift:37-48`

A 12-line GPIO blink loop left inside `ButtonShunt.init()` as a floating comment block.  
There is no surrounding method or conditional guard — it is structurally dead.

---

### A9. Commented-out `ModeSelect` cluster — `Matter/Clusters.swift:200-215`

```swift
// TODO: Do I add struct ModeSelect: MatterConcreteCluster {} here?
// struct ModeSelect: ...
```

Incomplete stub, never referenced. Per `CLAUDE.md`, ModeSelect was explicitly rejected as a device type.

---

### A10. Commented-out `FanModeSequenceValue` — `Matter/Attribute.swift:92-94`

```swift
// struct FanModeSequenceValue: MatterAttribute {
//   var attribute: UnsafeMutablePointer<esp_matter.attribute_t>
// }
```

The corresponding `AttributeID` in `Clusters.swift` is also commented out (line 189).  
Both can be removed together or both reinstated together.

---

## B. Attribute ID Inconsistencies

These mismatches mean the Swift event-routing layer and the C++ update layer are speaking different attribute IDs. At least one of each pair is wrong.

### B1. `FanMode` attribute ID — `Matter/Clusters.swift:187` vs `Matter/MatterInterface.cpp:73`

| Location | ID used |
|---|---|
| `Clusters.swift` `FanControl.AttributeID.fanMode` | `0x0000_0003` |
| `MatterInterface.cpp` `matter_fan_update_mode` update call | `0x0000_0000` |
| Matter spec Fan Control cluster, FanMode | `0x0000` |

The C++ layer is correct per spec. The Swift layer uses `0x0003` (which is `PercentCurrent` per spec).  
**Consequence:** When Apple Home writes FanMode (`0x0000`), the attribute callback fires with `attribute = 0x0000`. `Matter.Endpoint.Attribute.init(cluster:attribute:)` compares against `FanControl.AttributeID.fanMode.rawValue = 0x0003`. No match → the `.fanMode` case is never produced → `fanEndpoint.eventHandler` never receives a `.fanMode` event.

---

### B2. `PercentSetting` attribute ID — `Matter/Clusters.swift:190` vs `Matter/MatterInterface.cpp:81`

| Location | ID used |
|---|---|
| `Clusters.swift` `FanControl.AttributeID.percentSetting` | `0x0000_0002` |
| `MatterInterface.cpp` second `attribute::update` call | `0x0000_0006` |
| Matter spec Fan Control cluster, PercentSetting | `0x0002` |
| Matter spec Fan Control cluster, SpeedCurrent | `0x0006` |

The Swift layer uses the correct spec ID. The C++ layer writes to `0x0006` (`SpeedCurrent`), not `PercentSetting`.

---

### B3. `FanModeSequence` value — three different claims

| Location | Value |
|---|---|
| `MatterInterface.h` comment line 35 | `0x02` = Off/Low/High |
| `MatterInterface.cpp` line 37 (actual code) | `0x03` = Off/Low/High/Auto |
| `Main.swift` comment lines 3-4 | `0x02` = Off/Low/High |

The code sets `0x03`; the surrounding comments say `0x02`. Per the Matter Fan Control spec:  
`0x02` = Off/Low/High, `0x03` = Off/Low/Med/High.

---

## C. Redundant / Duplicated Logic

### C1. Identity mapping expressed twice — `main/Main.swift:16-35`

```swift
let modeForHardwareState: [UInt8] = [0, 1, 2, 3]  // hw index → FanMode

func hardwareStateForFanMode(_ mode: UInt8) -> UInt8 {
  switch mode {
  case 0: return 0
  case 1: return 1
  case 2: return 2
  case 3: return 3
  default: return 0
  }
}
```

Both encode the same trivial identity mapping (0→0, 1→1, 2→2, 3→3).  
`hardwareStateForFanMode` is the functional inverse of `modeForHardwareState`, but since the mapping is the identity, both return the same value for valid inputs.  
`modeForHardwareState` is used only in one call site (`fanEndpoint.updateFanMode(modeForHardwareState[Int(hwState)])`), which is equivalent to passing `hwState` directly.

---

### C2. Parallel GPIO setup functions — `Matter/MatterInterface.cpp:90-99` and `110-119`

`setup_fan_button_listen_gpio` and `setup_lamp_button_listen_gpio` are byte-for-byte identical in their `gpio_config_t` setup block — only the variable name and ISR handler differ:

```cpp
gpio_config_t cfg = {};
cfg.pin_bit_mask = 1ULL << gpio_num;
cfg.mode = GPIO_MODE_INPUT;
cfg.pull_up_en = GPIO_PULLUP_ENABLE;
cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
cfg.intr_type = GPIO_INTR_NEGEDGE;
gpio_config(&cfg);
```

This 7-line block appears twice verbatim.

---

### C3. Parallel ISR latch pattern — `Matter/MatterInterface.cpp:55-59, 102-128`

The fan and lamp each have:

- A `static volatile bool s_X_button_pressed` latch
- An `IRAM_ATTR X_button_isr_handler` that sets it
- A `matter_X_button_was_pressed()` polling function that reads and clears it

All three components are structurally identical for both buttons — only the names differ.

---

## D. Incomplete Implementations

### D1. `FanControl` cluster missing `attribute()` method — `Matter/Clusters.swift:182-198`

Every other `MatterConcreteCluster` (`Identify`, `OnOff`, `LevelControl`, `ColorControl`) defines:

```swift
func attribute<Attribute: MatterAttribute>(_ id: AttributeID<Attribute>) -> Attribute {
  Attribute(attribute: esp_matter.attribute.get_shim(cluster, id.rawValue))
}
```

`FanControl` does not. Its `AttributeID` constants exist but there is no way to read a `FanControl` attribute value through the Swift type system.

---

### D2. `Identify` cluster has no attribute ID constants — `Matter/Clusters.swift:65-82`

`Identify` defines `AttributeID<Attribute>` as a generic type but provides no static constants for any attribute (compare to `OnOff.AttributeID.state`, `LevelControl.AttributeID.currentLevel`, etc.).

---

### D3. Multi-press sequence logic is commented out — `main/Main.swift:73-77`

```swift
// let presses = (Int(targetHw) - Int(hwState) + 4) % 4
// for i in 0..<presses {
//   if i > 0 { delay_ms(200) }
//   fanButton.press()
// }
fanButton.press()   // ← always fires exactly one press
```

Per `CLAUDE.md`, the correct behavior is `(target - current + 4) % 4` presses.  
The current stub fires one press unconditionally and overwrites `hwState` with `targetHw`,  
leaving the hardware out of sync after any mode change that requires more than one press.

---

## F. Debug Print Statements in Production Paths

All `print()` calls listed below are in hot paths (main loop or every button press/release) and are emoji-heavy strings that will appear on every UART log:

| File | Line(s) | Context |
|---|---|---|
| `Main.swift` | 41 | Boot message with 30 emoji |
| `Main.swift` | 65 | FanMode event received |
| `Main.swift` | 68 | PercentSetting event received |
| `Main.swift` | 70 | Fallthrough default (error path) |
| `Main.swift` | 106 | Physical fan button press detected |
| `Main.swift` | 108 | `hwState` value trace |
| `Main.swift` | 113 | Physical lamp button press detected |
| `ButtonShunt.swift` | 52 | Every GPIO press |
| `ButtonShunt.swift` | 55 | Every GPIO release |

---

## Summary Table

| ID | File(s) | Type | Severity |
|---|---|---|---|
| A1 | MatterInterface.cpp:41 | Unreachable code | Low |
| A2 | MatterInterface.cpp:69-71 | Dead comment | Low |
| A3 | main/LED.swift | Orphaned file | Medium |
| A4 | main/Relay.swift | Orphaned file | Low |
| A5 | BridgingHeader.h:49 | Unused include | Low |
| A6 | Matter.swift:96-131 | Dead attribute cases | Low |
| A7 | Matter.swift:171-173 | Dead comment | Low |
| A8 | ButtonShunt.swift:37-48 | Dead comment block | Low |
| A9 | Clusters.swift:200-215 | Dead comment block | Low |
| A10 | Attribute.swift:92-94 | Dead comment | Low |
| **B1** | **Clusters.swift:187 / MatterInterface.cpp:73** | **Attribute ID mismatch (FanMode)** | **High — fanMode events are never routed** |
| **B2** | **Clusters.swift:190 / MatterInterface.cpp:81** | **Attribute ID mismatch (PercentSetting)** | **Medium — wrong attribute updated** |
| B3 | MatterInterface.h:35 / .cpp:37 / Main.swift:3-4 | Comment vs code mismatch | Low |
| C1 | Main.swift:16-35 | Redundant identity mapping | Low |
| C2 | MatterInterface.cpp:91-97, 111-117 | Duplicated GPIO config block | Low |
| C3 | MatterInterface.cpp:55-128 | Duplicated latch pattern | Low |
| **D3** | **Main.swift:73-78** | **Incomplete multi-press logic** | **High — hardware state desync** |
| D1 | Clusters.swift:182-198 | Missing `attribute()` method | Medium |
| D2 | Clusters.swift:65-82 | Missing attribute ID constants | Low |
| E1 | Node.swift:20 | Typo in protocol name | Low |
| F | Main.swift, ButtonShunt.swift | Debug prints in hot paths | Low |
