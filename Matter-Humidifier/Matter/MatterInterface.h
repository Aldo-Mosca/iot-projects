//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift open source project
//
// Copyright (c) 2024 Apple Inc. and the Swift project authors.
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
//
//===----------------------------------------------------------------------===//
// GNU C++ interfaces do not work well with Swift for certain types, so let's use some simple C++ shims.
// For example, uint32_t gets imported as UInt and not CUnsignedLong (as defined in ESP IDF).
namespace esp_matter {
  namespace attribute {
    typedef esp_err_t (*callback_t_shim)(callback_type_t type, uint16_t endpoint_id, unsigned int cluster_id,
                                         unsigned int attribute_id, esp_matter_attr_val_t *val, void *priv_data);
    esp_err_t set_callback_shim(callback_t_shim callback);
  }

  namespace cluster {
    cluster_t *get_shim(endpoint_t *endpoint, unsigned int cluster_id);
  }

  namespace attribute {
    attribute_t *get_shim(cluster_t *cluster, unsigned int attribute_id);
  }
}

// Recomissioning causes failures with reference semantics so this is done as a function implemented in C++.
// Ideally this would be done by changing some of the headers in ESP Matter to have proper Swift annotations.
void recomissionFabric();

// Creates a fan endpoint (device type 0x0044) configured for the humidifier:
//   FanModeSequence = Off/Low/High (0x02), percent_setting = null (no slider)
esp_matter::endpoint_t *create_humidifier_fan_endpoint(
    esp_matter::node_t *node, void *priv_data);

// Creates an air purifier endpoint (device type 0x002D). Same FanControl
// cluster as the fan endpoint, just a different device-type ID so Apple
// Home renders the accessory as an air purifier.
esp_matter::endpoint_t *create_humidifier_air_purifier_endpoint(
    esp_matter::node_t *node, void *priv_data);

// Creates an on/off light endpoint (device type 0x0100).
esp_matter::endpoint_t *create_on_off_light_endpoint(
    esp_matter::node_t *node, void *priv_data);

// Creates a Mode Select endpoint (device type 0x0027) with a hardcoded
// 4-mode list (Off / High / Low / Night) registered as the global
// SupportedModesManager. CurrentMode values match the hardware K1 cycle.
esp_matter::endpoint_t *create_humidifier_mode_select_endpoint(
    esp_matter::node_t *node, void *priv_data);

// Creates a Generic Switch endpoint (device type 0x000F) with the Switch
// cluster (0x003B) configured for MomentarySwitch only. Apple Home will
// surface this as a physical button usable as an automation trigger.
esp_matter::endpoint_t *create_humidifier_switch_endpoint(
    esp_matter::node_t *node, void *priv_data);

extern "C" {

// pdMS_TO_TICKS is a C macro and cannot be called directly from Swift.
void delay_ms(uint32_t ms);

// Update the Fan Control cluster's FanMode attribute in the Matter data model.
// Call this when a physical K1 press changes the firmware state.
// fan_mode values: 0=Off, 1=Low, 2=Med(Night), 3=High.
esp_err_t matter_fan_update_mode(uint16_t endpoint_id, uint8_t fan_mode);

// Update the OnOff cluster's OnOff attribute in the Matter data model.
// Call this to sync physical LED state back to Matter after a local change.
esp_err_t matter_onoff_update(uint16_t endpoint_id, bool on);

// Update the Mode Select cluster's CurrentMode attribute in the Matter data model.
// Call this when a physical K1 press changes the firmware state.
// mode values: 0=Off, 1=High, 2=Low, 3=Night (matches hwState directly).
esp_err_t matter_mode_select_update_current_mode(uint16_t endpoint_id, uint8_t mode);

// Fire a single momentary press on the Switch cluster: toggle CurrentPosition
// 1 → 0 and emit the InitialPress event. Call this when the physical lamp
// button is pressed so Home automations can react.
esp_err_t matter_switch_press(uint16_t endpoint_id);

// Configure a GPIO as input with a falling-edge interrupt to detect physical K1 (lamp) presses.
// Configure GPIO gpio_num as a rising-edge ISR input to detect the 100 Hz scan
// pulses the humidifier MCU emits on the S2 line. Absence of pulses for > 50 ms
// indicates a physical S2 press. Call once at startup; installs ISR service if
// not already done.
void setup_lamp_listen_gpio(int32_t gpio_num);

// Scans candidate GPIOs for the 100 Hz S2 signal and logs results.
// Call once at startup before setup_lamp_listen_gpio().
void matter_gpio_scan_for_signal(void);

// Returns the cumulative count of edges seen on the S2 scan line since boot.
// A press is detected when this count stops incrementing between main-loop ticks.
uint32_t matter_lamp_pulse_count(void);

// ===== Panel-LED sensing for MIST state (replaces K2 button listen) =====

// Configure the three GPIOs used to sense MIST mode from the panel LED encoding:
//   row_gpio  — ADC input, must be ADC1-capable (XIAO ESP32-C6: GPIO0..GPIO6;
//               only D1=GPIO1 is free after the shunts on D0/D2)
//   col_a_gpio, col_b_gpio — digital inputs
// Call this once at startup, after setup_*_listen_gpio() (which installs the
// shared GPIO ISR service the digital inputs share).
void setup_mist_panel_sensors(int32_t row_gpio, int32_t col_a_gpio, int32_t col_b_gpio);

// Read the current MIST hardware state by sampling the panel LED encoding.
// Returns: 0 = Off, 1 = On, 2 = 1H, 3 = 3H, 4 = 6H.
// Safe to call from the main loop at ~5 Hz; takes < 1 ms.
uint8_t matter_read_mist_state(void);

} // extern "C"
