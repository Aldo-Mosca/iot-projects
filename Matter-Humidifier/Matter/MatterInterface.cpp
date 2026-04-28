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

#include "BridgingHeader.h"

esp_err_t esp_matter::attribute::set_callback_shim(callback_t_shim callback) {
  return set_callback((callback_t)callback);
}

esp_matter::cluster_t *esp_matter::cluster::get_shim(esp_matter::endpoint_t *endpoint, unsigned int cluster_id) {
  return get(endpoint, (uint32_t)cluster_id);
}

esp_matter::attribute_t *esp_matter::attribute::get_shim(esp_matter::cluster_t *cluster, unsigned int attribute_id) {
  return get(cluster, (uint32_t)attribute_id);
}

// ---- Fan endpoint factory ----
//
// FanModeSequence 0x00 = Off / Low / Med / High
// percent_setting = nullptr → null attribute value → no speed slider

esp_matter::endpoint_t *create_humidifier_fan_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::fan::config_t config;
  config.fan_control.fan_mode          = 0;     // Off
  config.fan_control.fan_mode_sequence = 0x03;  // Off/Low/High/Auto
  config.fan_control.percent_setting   = static_cast<uint8_t>(0);  // Off initially
  config.fan_control.percent_current   = 0;
  return esp_matter::endpoint::fan::create(node, &config, 0x00, priv_data);
}

// ---- ISR latch ----

static volatile bool s_button_pressed = false;

static void IRAM_ATTR button_isr_handler(void *) {
  s_button_pressed = true;
}

extern "C" {

void delay_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t matter_fan_update_mode(uint16_t endpoint_id, uint8_t fan_mode) {
  // Update FanMode (0x0000)
  esp_matter_attr_val_t val = esp_matter_uint8(fan_mode);
  esp_err_t err = esp_matter::attribute::update(endpoint_id, 0x00000202, 0x00000000, &val);
  if (err != ESP_OK) return err;
  // Also sync PercentSetting (0x0002) so Apple Home slider reflects the state.
  // FanModeSequence is Off/Low/High/Auto; Night has no named slider position
  // so it reports back as High (67%) to keep the slider at a valid anchor.
  // Mapping: Off=0%, Low=33%, Med(Night)=67%, High=67%
  static const uint8_t kPercent[4] = {0, 33, 67, 67};
  val = esp_matter_uint8(fan_mode < 4 ? kPercent[fan_mode] : 0);
  return esp_matter::attribute::update(endpoint_id, 0x00000202, 0x00000002, &val);
}

void setup_button_listen_gpio(int32_t gpio_num) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << gpio_num;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_NEGEDGE;
  gpio_config(&cfg);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(static_cast<gpio_num_t>(gpio_num), button_isr_handler, nullptr);
}

bool matter_button_was_pressed(void) {
  if (s_button_pressed) {
    s_button_pressed = false;
    return true;
  }
  return false;
}

} // extern "C"

void recomissionFabric() {
  if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
    chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(300);
    if (!commissionMgr.IsCommissioningWindowOpen()) {
      commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds, chip::CommissioningWindowAdvertisement::kDnssdOnly);
    }
  }
}
