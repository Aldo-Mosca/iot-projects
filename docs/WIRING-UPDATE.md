# WIRING UPDATE — Panel-LED Sensing for MIST State

**Date:** 2026-06-02 (updated 2026-06-10 — S2 pulse-absence LIGHT detection; D8/GPIO19 pin assignment; UART-pin mapping corrections)
**Hardware:** Seeed Studio XIAO ESP32-C6 + new humidifier model (2 buttons, 4 mode LEDs, JST PH002 7-pin panel cable)

This document captures the **new** wiring after switching MIST-state detection from the K1/K2 button-listen-ISR approach to direct panel-LED state sensing. The old approach proved unreliable on the new control board (noise-driven spurious ISRs, no clean physical-button line on the cable).

---

## What changed at a glance

| Function | Before | After |
|---|---|---|
| MIST state sync (device → Matter) | Listen-ISR on S1 line — noisy, dropped events, false triggers | Poll 3 panel-LED encoding lines (L2/L3/L4) every main-loop tick |
| LIGHT press detect (device → Matter) | Listen-ISR on S2 line — noisy | Negedge ISR on L1 (unreliable — see 2026-06-10 update) → replaced by **pulse-absence detection** on the humidifier MCU S2 scan line (D8/GPIO19): ANYEDGE ISR counts 100 Hz pulses; no count change over 200 ms means button is held |
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

| Silkscreen | GPIO | ADC1 channel routed? | Notes |
|---|---|---|---|
| D0 | 0 | yes (CH0) | |
| D1 | 1 | yes (CH1) | |
| D2 | 2 | yes (CH2) | |
| D5 | 23 | no | |
| D6 | 6 | no | **UART0 TX** (console/debug output) — UART peripheral owns the IO MUX; GPIO ISR cannot receive external signals here |
| D7 | 16 | no | **UART secondary TX** — same caveat as D6; GPIO edge interrupts won't fire reliably |
| D8 | 19 | no | General I/O safe; currently used for S2 scan-line pulse detection (`lampListenGPIO`) |
| D9 | 20 | no | General I/O safe |
| D10 | 18 | no | |

Although ADC1 has channels 0–6 at the chip level (GPIO0–GPIO6), **only D0/D1/D2 are routed out as ADC pins on the XIAO board** — the other channels' underlying GPIOs (GPIO3–GPIO6) aren't exposed on the silkscreen as analog inputs.

**UART-claimed pins (D6/D7):** `gpio_get_level` on GPIO6 or GPIO16 returns 0 regardless of external signal level, and GPIO edge ISRs on these pins don't fire, because the UART peripheral holds the IO MUX ownership. Confirmed by direct experiment: routing the S2 wire to D6 or D7 produced 0/200000 HIGHs in a polling scan even when the oscilloscope showed live 100 Hz signal.

**Pin-safety guidance from the Seeed forum** ([Technical Reference Manual vs the datasheet](https://forum.seeedstudio.com/t/technical-reference-manual-vs-the-datasheet/292511/3)):

- **D0–D4:** marked unsafe for general I/O — used during boot strapping and SPI flash access. **Safe as outputs** that drive high-impedance loads (our MOSFET gates do — gate input is essentially infinite impedance), because the gate floats during the brief boot window before `app_main` configures the pin. **Avoid for inputs** that would be driven externally — *with one necessary exception below*.
- **D5–D10:** safe for general I/O.

**Necessary exception — D1 for the ADC input:** L2 needs analog reading. Of the three routed ADC pins (D0/D1/D2), D0 and D2 are already driving the MOSFET shunts, leaving D1 as the only option. The "unsafe" flag on D1 is real but manageable here: at boot the divided L2 voltage is ≤ 1.55 V (often 0 V if the device is off), the ESP32-C6 strapping briefly samples GPIO1 at reset, and once boot completes the pin is free for app use. If you ever see boot failures correlate with humidifier state at power-on, add a series resistor or a small clamping diode and let the panel side dominate after boot.

| Function | XIAO pin | GPIO | Direction | Wire to |
|---|---|---|---|---|
| LIGHT button shunt (existing) | **D0** | 0 | OUTPUT | LIGHT MOSFET gate |
| MIST button shunt (existing) | **D2** | 2 | OUTPUT | MIST MOSFET gate |
| **L2** MIST row sensor (ADC) | **D1** | 1 | INPUT (ADC1_CH1) | L2 via 100 k / 100 k divider |
| **S2 scan-line** LIGHT press detect | **D8** | 19 | INPUT (ANYEDGE ISR) | S2 humidifier MCU scan line via 10 kΩ / 15 kΩ divider (see note) |
| **L3** MIST col A sensor | **D10** | 18 | INPUT (digital) | L3 via 100 k / 100 k divider |
| **L4** MIST col B sensor | **D5** | 23 | INPUT (digital) | L4 via 100 k / 100 k divider |
| Common ground | **GND** | — | — | L5 |
| Power in (optional) | **5V** | — | power in | L6 |

**Note on D8 = GPIO19 (S2 scan-line):** The humidifier MCU emits 100 Hz / ~33 µs rising pulses on the S2 scan line during idle. Physical button press suppresses the pulses entirely (line held LOW). The XIAO registers this via an ANYEDGE ISR (`lamp_listen_isr_handler`) that increments a counter on every edge. The main loop samples the counter at 200 ms intervals; if the count has not changed, the button is considered pressed. Voltage divider (10 kΩ series + 15 kΩ to GND) scales the 4.6 V panel pulses to ~2.76 V — safely within the ESP32-C6 3.6 V GPIO absolute maximum.

**Note on D7 = GPIO16:** Previously listed (incorrectly) as GPIO17 and trialled as the S2 input. GPIO16 is a UART-claimed pin — the UART peripheral holds the IO MUX and blocks GPIO edge detection. Confirmed by oscilloscope (live 100 Hz signal visible) versus GPIO scan (0/200000 HIGHs). Do not use D6 or D7 for external edge-interrupt inputs.

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

### L2 sees the ADC; L3/L4 see digital GPIOs; S2 scan line uses a different divider

The same 100 kΩ / 100 kΩ divider topology works for L2, L3, and L4. The S2 scan-line input is different:

- **L2 → ADC1 channel 1** (D1/GPIO1). Configured with **12 dB attenuation** (~0…3.3 V usable range) and curve-fitting calibration. The firmware reads in millivolts and thresholds at 200 mV (off) and 1300 mV (which mode pair).
- **L3, L4 → digital GPIO**. After the divider, 0.15 V (LED active) and 2.25 V (LED inactive) are unambiguously below / above the GPIO digital threshold (~1.4 V on 3.3 V logic).
- **S2 scan line → D8/GPIO19 — 10 kΩ / 15 kΩ divider.** The S2 line carries 100 Hz / ~33 µs pulses at ~4.6 V. The lower-impedance divider (vs. 100 kΩ / 100 kΩ) ensures the short pulses are not rounded off by parasitic capacitance on the breadboard run. Circuit:

```
S2 humidifier scan line
    |
    +---[ 10 kΩ ]---+---[ 15 kΩ ]---  GND (L5 / XIAO GND, common)
                    |
                    +--- to D8 (GPIO19)
```

  Output voltage: 15 kΩ / (10 kΩ + 15 kΩ) × 4.6 V ≈ **2.76 V** — safely within the ESP32-C6 3.6 V GPIO absolute maximum.

**Note on L1 (lightButtonInputGPIO / GPIO17):** The panel L1 line (LIGHT button, active LOW on press) was trialled as the LIGHT press detect source. The panel drives L1 with an active pull-down at all times, making the divided voltage ambiguous at the Schmitt threshold in both idle and pressed states. This approach was abandoned; L1 wiring can remain connected for voltage monitoring but is not used by the firmware for press detection.

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
3. **Press LIGHT once.** Should see `[HUMI] 💡 S2 physical press detected` in the log and the light tile in Apple Home should toggle. The detection relies on the 100 Hz S2 scan-line pulses going absent during the button hold: if the lamp count debug print is temporarily re-enabled (`// print("[HUMI] 🔦 lamp count=...")`), you should see the counter incrementing between ticks at idle and frozen during a physical press.
4. **Long-press either button (1+ s).** Device turns off. Should see `[HUMI] 💧 mist state change N → 0`.
5. **From Apple Home: toggle the mist tile.** Within ~200 ms you should see the MOSFET pulse, the panel LED change, and the corresponding `mist state change` line confirm the state was sensed back.
6. **From Apple Home: toggle the light tile.** The LIGHT MOSFET should pulse (D0/GPIO0) and the humidifier lamp should turn on or off.

---

## Migration notes from the previous wiring

The following constants and code paths are superseded but kept commented for reference:

- `lampListenGPIO` (was on D1/GPIO1 in the very first wiring, then moved to D7/GPIO17 as a negedge ISR on L1) → now on **D8/GPIO19**, connected to the humidifier MCU's S2 scan line. Detection method changed from negedge ISR on the panel button line to pulse-absence counting on the MCU scan line (see "S2 scan-line" note above).
- `fanListenGPIO` (was listed as GPIO21 — that was an error; XIAO D-to-GPIO mapping is not 1:1 past D2) → no longer used. The new `lightButtonInputGPIO` (L1 direct panel line) lives on GPIO17 but is not actively used for press detection.
- `setup_fan_button_listen_gpio()` and `matter_fan_button_was_pressed()` are still defined in `MatterInterface.cpp` but no longer called.
- `setup_lamp_listen_gpio()` now calls `gpio_install_isr_service()` **before** enabling the interrupt, then configures the pin with `GPIO_INTR_DISABLE`, registers the handler via `gpio_isr_handler_add()`, and only then enables ANYEDGE. The prior ordering (configure-with-interrupt first, then install service) caused unhandled edges during the startup window to latch the interrupt-status register and silently block all subsequent edge detection.
- `Main.swift`'s main loop now polls `matter_read_mist_state()` per iteration and only reports state *changes* to Matter, instead of edge-detecting transient button events.

If you ever want to roll back to the old listen-ISR approach (e.g., if hardware is revised again), the previous `setup_*_listen_gpio` calls are commented out in `Main.swift` and can be reinstated by uncommenting them and reverting the constants in `ButtonShunt.swift`.