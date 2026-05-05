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

// Creates an on/off light endpoint (device type 0x0100).
esp_matter::endpoint_t *create_on_off_light_endpoint(
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

// Configure a GPIO as input with a falling-edge interrupt to detect physical K2 (fan) presses.
// Also installs the GPIO ISR service — call this before setup_lamp_button_listen_gpio.
void setup_fan_button_listen_gpio(int32_t gpio_num);

// Returns true (and clears the latch) if a physical K2 (fan) press was detected since last call.
bool matter_fan_button_was_pressed(void);

// Configure a GPIO as input with a falling-edge interrupt to detect physical K1 (lamp) presses.
// ISR service must already be installed (call setup_fan_button_listen_gpio first).
void setup_lamp_button_listen_gpio(int32_t gpio_num);

// Returns true (and clears the latch) if a physical K1 (lamp) press was detected since last call.
bool matter_lamp_button_was_pressed(void);

} // extern "C"
