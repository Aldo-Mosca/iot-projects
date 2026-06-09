# WIRING UPDATE — Panel-LED Sensing for MIST State

**Date:** 2026-06-02
**Hardware:** Seeed Studio XIAO ESP32-C6 + new humidifier model (2 buttons, 4 mode LEDs, JST PH002 7-pin panel cable)

This document captures the **new** wiring after switching MIST-state detection from the K1/K2 button-listen-ISR approach to direct panel-LED state sensing. The old approach proved unreliable on the new control board (noise-driven spurious ISRs, no clean physical-button line on the cable).

---

## What changed at a glance

| Function | Before | After |
|---|---|---|
| MIST state sync (device → Matter) | Listen-ISR on S1 line — noisy, dropped events, false triggers | Poll 3 panel-LED encoding lines (L2/L3/L4) every main-loop tick |
| LIGHT press detect (device → Matter) | Listen-ISR on S2 line — noisy | Negedge ISR on the L1 panel-cable line — clean direct-from-button signal |
| MIST shunt (Matter → device) | MOSFET on K2 | **Unchanged** — same MOSFET on K2 |
| LIGHT shunt (Matter → device) | MOSFET on K1 | **Unchanged** — same MOSFET on K1 |
| ESP power | USB-C from laptop | Optionally from panel cable L6 (5 V) once verified |

The two output paths (MOSFET shunts driving S1/S2 contacts) are unchanged. Only the input/sensing paths are new.

---

## Panel cable pin map

The humidifier panel connects to the main board through a **JST PH002 7-pin** cable. A debug splice cable taps all 7 lines without breaking the circuit.

| Pin | Name in firmware | Function | Voltage / behavior |
|---|---|---|---|
| L1 | `lightButtonInputGPIO` | LIGHT (S2) button line — active LOW on press | Idle ~5 V (panel pull-up), momentarily 0 V on press |
| L2 | `mistRowGPIO` | MIST row selector — 3-level analog | ~0 V (off), ~2.2 V (modes 3H/6H), ~3.1 V (modes On/1H) |
| L3 | `mistColAGPIO` | MIST column A — digital | LOW (~0.3 V) when mode is On or 3H; HIGH (~4.5 V) otherwise |
| L4 | `mistColBGPIO` | MIST column B — digital | LOW (~0.25 V) when mode is 1H or 6H; HIGH (~4.7 V) otherwise |
| L5 | — | GND | Reference for all signals; tie to XIAO GND |
| L6 | — | +5 V panel power | Source for XIAO 5 V pin (optional — see "Powering the XIAO" below) |
| L7 | — | (unused) | ~55 mV @ 50 kHz noise/SMPS pickup, no useful signal |

### MIST encoding table

```
hwState  Mode  L2 (panel)   L3            L4
   0     Off   ~0 V         inactive      inactive
   1     On    ~3.1 V       active (LOW)  inactive (HIGH)
   2     1H    ~3.1 V       inactive      active (LOW)
   3     3H    ~2.2 V       active (LOW)  inactive (HIGH)
   4     6H    ~2.2 V       inactive      active (LOW)
```

Long-press (1+ second) on either panel button turns the whole device off (mist → 0, light → off).

---

## XIAO ESP32-C6 pin assignment

On the Seeed XIAO ESP32-C6, **the silkscreen D-numbers do NOT generally map 1:1 to GPIO numbers**. Only D0/D1/D2 happen to match (D0=GPIO0, etc.); from D3 onwards the mapping is non-obvious and must be looked up in Seeed's datasheet. Confirmed from the datasheet:

| Silkscreen | GPIO | ADC1 channel routed? |
|---|---|---|
| D0 | 0 | yes (CH0) |
| D1 | 1 | yes (CH1) |
| D2 | 2 | yes (CH2) |
| D5 | 23 | no |
| D7 | 17 | no |
| D10 | 18 | no |

Although ADC1 has channels 0–6 at the chip level (GPIO0–GPIO6), **only D0/D1/D2 are routed out as ADC pins on the XIAO board** — the other channels' underlying GPIOs (GPIO3–GPIO6) aren't exposed on the silkscreen as analog inputs.

**Pin-safety guidance from the Seeed forum** ([Technical Reference Manual vs the datasheet](https://forum.seeedstudio.com/t/technical-reference-manual-vs-the-datasheet/292511/3)):

- **D0–D4:** marked unsafe for general I/O — used during boot strapping and SPI flash access. **Safe as outputs** that drive high-impedance loads (our MOSFET gates do — gate input is essentially infinite impedance), because the gate floats during the brief boot window before `app_main` configures the pin. **Avoid for inputs** that would be driven externally — *with one necessary exception below*.
- **D5–D10:** safe for general I/O.

**Necessary exception — D1 for the ADC input:** L2 needs analog reading. Of the three routed ADC pins (D0/D1/D2), D0 and D2 are already driving the MOSFET shunts, leaving D1 as the only option. The "unsafe" flag on D1 is real but manageable here: at boot the divided L2 voltage is ≤ 1.55 V (often 0 V if the device is off), the ESP32-C6 strapping briefly samples GPIO1 at reset, and once boot completes the pin is free for app use. If you ever see boot failures correlate with humidifier state at power-on, add a series resistor or a small clamping diode and let the panel side dominate after boot.

| Function | XIAO pin | GPIO | Direction | Wire to |
|---|---|---|---|---|
| LIGHT button shunt (existing) | **D0** | 0 | OUTPUT | LIGHT MOSFET gate |
| MIST button shunt (existing) | **D2** | 2 | OUTPUT | MIST MOSFET gate |
| **L2** MIST row sensor (ADC) | **D1** | 1 | INPUT (ADC1_CH1) | L2 via 100 k / 100 k divider |
| **L1** LIGHT button sensor | **D7** | 17 | INPUT (negedge ISR) | L1 via external 1 kΩ pull-up to 3.3 V (see note) |
| **L3** MIST col A sensor | **D10** | 18 | INPUT (digital) | L3 via 100 k / 100 k divider |
| **L4** MIST col B sensor | **D5** | 23 | INPUT (digital) | L4 via 100 k / 100 k divider |
| Common ground | **GND** | — | — | L5 |
| Power in (optional) | **5V** | — | power in | L6 |

**Note on D7 = GPIO17:** GPIO17 is the ESP32-C6 chip's default UART0 RX pin. On the XIAO this doesn't matter — `idf.py monitor` uses USB-Serial-JTAG (GPIO12/13) rather than UART0 — but if you ever see weird serial behavior, that's where to look first.

---

## Voltage divider circuits

All four panel sensor lines (L1–L4) carry up to ~5 V from the panel side. The ESP32-C6 GPIO maximum input voltage is **3.6 V**, so every sensor wire needs a divider.

Standard divider per sensor wire — same circuit four times:

```
Panel pin (L1, L2, L3, or L4)
    |
    +---[ 100 kΩ ]---+---[ 100 kΩ ]---  GND (L5 / XIAO GND, common)
                    |
                    +--- to XIAO GPIO
                    |
                    +---[ 100 nF ]---  GND   (optional but recommended)
```

- **Divides voltage by 2.** 5 V panel side → 2.5 V at ESP. 4.5 V → 2.25 V. 3.1 V → 1.55 V. 2.2 V → 1.10 V. 0 V → 0 V.
- **Presents 200 kΩ load** to the panel — invisible to the panel's driver circuit.
- **Optional 100 nF cap to GND** at the ESP input filters HF noise. Forms ~10 µs RC time constant with the 100 kΩ source impedance — invisible to all our signal timing but kills RFI pickup.

### L2 sees the ADC; L3/L4 see digital GPIOs; L1 uses an external pull-up

The same divider topology works for L2, L3, and L4. L1 is different:

- **L2 → ADC1 channel 1** (D1/GPIO1). Configured with **12 dB attenuation** (~0…3.3 V usable range) and curve-fitting calibration. The firmware reads in millivolts and thresholds at 200 mV (off) and 1300 mV (which mode pair).
- **L3, L4 → digital GPIO**. After the divider, 0.15 V (LED active) and 2.25 V (LED inactive) are unambiguously below / above the GPIO digital threshold (~1.4 V on 3.3 V logic).
- **L1 → D7/GPIO17 — external 1 kΩ pull-up, no voltage divider.** The panel drives L1 with an active ~1 kΩ pull-down at all times (both idle and pressed), making the 100 kΩ/100 kΩ divider useless (idle output ≈ 0 V). An external 1 kΩ pull-up from L1 to XIAO 3.3 V brings idle voltage to ~1.65 V. When the button is pressed it adds ~6 kΩ parallel to the panel pull-down, shifting voltage to ~1.53 V — only 120 mV below idle. Both values sit at the ESP32-C6 Schmitt threshold, making reliable edge detection impossible. **LIGHT panel-press detection is one-way only (Home → device works; device → Home does not).** The ISR is registered in firmware but fires spuriously; the `setup_lamp_button_listen_gpio()` call in `main/Main.swift` should be commented out on the next code change.

---

## Powering the XIAO from L6

L6 carries **+5 V** from the humidifier's internal supply. Wiring is two lines:

```
L6 (+5 V) ─────────► XIAO 5V pad
L5 (GND)  ─────────► XIAO GND pad
```

Before committing to L6 power, verify the humidifier's 5 V rail can spare the XIAO's current:

1. Power up the humidifier with no XIAO connected. Confirm normal operation.
2. Connect L6 → 5V and L5 → GND. Power the humidifier. XIAO should boot, humidifier should still run normally.
3. Trigger WiFi or BLE commissioning on the XIAO (peak current ~300 mA). If the humidifier flickers, resets, the fan stutters, or the LEDs blink unexpectedly, the rail is too weak — use external USB power for the XIAO instead.

**Optional ride-through capacitor:** if the rail is marginal but not catastrophic, a **470 µF–1000 µF electrolytic** from L6 to L5 right at the XIAO 5 V pin smooths out current spikes. Watch polarity.

---

## Common-ground requirement

The XIAO and the humidifier panel **must share GND**. Without this, the listen-side voltages float relative to each other, GPIO inputs cross the negedge threshold randomly, and you get the spurious-press loop the previous wiring suffered from. Two ways to make it:

1. **Through L5 + L6 power feed**: connecting the XIAO 5 V to L6 and XIAO GND to L5 establishes the common ground automatically.
2. **Through a dedicated GND wire**: even if you power the XIAO from USB instead of L6, wire L5 directly to a XIAO GND pad. Don't rely on USB-to-mains-PE to provide ground continuity to the humidifier.

---

## Verification checklist

After wiring, with the device powered on:

1. **No physical interaction.** `idf.py monitor` should be quiet — no `[HUMI] 💧 mist state change` lines. If you see continuous state-change messages with no panel interaction, the dividers are off or the ground is bad. (Spurious `[HUMI] 💡 LIGHT button press detected` lines may appear even at idle due to the L1 Schmitt-threshold issue — this is a known limitation.)
2. **Press MIST once.** Should see exactly one `[HUMI] 💧 mist state change 0 → 1` line (or whatever the new state is). Each subsequent press should produce exactly one change line.
3. **Press LIGHT once.** Physical panel LIGHT presses are NOT reliably detected — see "L1 LIGHT button detection" note above. Skip this verification step.
4. **Long-press either button (1+ s).** Device turns off. Should see `[HUMI] 💧 mist state change N → 0`.
5. **From Apple Home: toggle the mist tile.** Within ~200 ms you should see the MOSFET pulse, the panel LED change, and the corresponding `mist state change` line confirm the state was sensed back.
6. **From Apple Home: toggle the light tile.** The LIGHT MOSFET should pulse (D0/GPIO0) and the humidifier lamp should turn on or off.

---

## Migration notes from the previous wiring

The following constants and code paths are superseded but kept commented for reference:

- `lampListenGPIO` (was on D1/GPIO1) → no longer used. The new `mistRowGPIO` lives on **D1/GPIO1** (same physical pin, repurposed from listen ISR to ADC input).
- `fanListenGPIO` (was listed as GPIO21 — that was an error; XIAO D-to-GPIO mapping is not 1:1 past D2) → no longer used. The new `lightButtonInputGPIO` lives on **D7/GPIO17**.
- `setup_fan_button_listen_gpio()` and `matter_fan_button_was_pressed()` are still defined in `MatterInterface.cpp` but no longer called.
- `setup_lamp_button_listen_gpio()` and `matter_lamp_button_was_pressed()` are still in use, now backing the LIGHT button input on its new GPIO. The function was made idempotent re: `gpio_install_isr_service()` so it can be called standalone.
- `Main.swift`'s main loop now polls `matter_read_mist_state()` per iteration and only reports state *changes* to Matter, instead of edge-detecting transient button events.

If you ever want to roll back to the old listen-ISR approach (e.g., if hardware is revised again), the previous `setup_*_listen_gpio` calls are commented out in `Main.swift` and can be reinstated by uncommenting them and reverting the constants in `ButtonShunt.swift`.
