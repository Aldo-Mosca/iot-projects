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
#include <app/clusters/mode-select-server/supported-modes-manager.h>
#include <app/clusters/switch-server/switch-server.h>
#include <cstring>

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
// FanModeSequence 0x02 = Off / Low / High  (no Auto)
// percent_setting = nullptr → null attribute value → no speed slider
// No multi_speed feature added → FeatureMap SPD bit = 0 → Apple Home renders
// discrete mode buttons (Off / Low / High) instead of a continuous slider.

esp_matter::endpoint_t *create_humidifier_fan_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::fan::config_t config;
  config.fan_control.fan_mode          = 0;       // Off
  config.fan_control.fan_mode_sequence = 0x03;    // Off/Low/High/Auto
  config.fan_control.percent_setting   = static_cast<uint8_t>(0);  // Off initially
  config.fan_control.percent_current   = 0;
  return esp_matter::endpoint::fan::create(node, &config, 0x00, priv_data);
  return esp_matter::endpoint::fan::create(node, &config, 0x00, priv_data);
}

// ---- On/Off Light endpoint factory ----

esp_matter::endpoint_t *create_on_off_light_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::on_off_light::config_t config;
  config.on_off.on_off = false;
  return esp_matter::endpoint::on_off_light::create(node, &config, 0x00, priv_data);
}

// ---- Mode Select endpoint factory ----
//
// Mode Select cluster (0x0050) requires a globally-registered SupportedModesManager
// to validate ChangeToMode commands. We hardcode 5 modes that match the hardware
// K1 cycle so currentMode == hwState directly:
//   0 = Off, 1 = On, 2 = 1H, 3 = 3H, 4 = 6H

namespace {

namespace ModeSel = chip::app::Clusters::ModeSelect;
using SemanticTagType = ModeSel::Structs::SemanticTagStruct::Type;
using ModeOptionType  = ModeSel::Structs::ModeOptionStruct::Type;

// One placeholder semantic tag, shared by all modes (mfgCode=0, value=0).
// Apple Home renders modes by label; semanticTags is required by spec but
// the values here aren't interpreted by any client we care about.
SemanticTagType sTags[] = {
  { static_cast<chip::VendorId>(0), static_cast<uint16_t>(0) },
};

const ModeOptionType sHumidifierModes[5] = {
  { chip::CharSpan::fromCharString("Off"), 0,
    chip::app::DataModel::List<const SemanticTagType>(sTags, 1) },
  { chip::CharSpan::fromCharString("On"),  1,
    chip::app::DataModel::List<const SemanticTagType>(sTags, 1) },
  { chip::CharSpan::fromCharString("1H"),  2,
    chip::app::DataModel::List<const SemanticTagType>(sTags, 1) },
  { chip::CharSpan::fromCharString("3H"),  3,
    chip::app::DataModel::List<const SemanticTagType>(sTags, 1) },
  { chip::CharSpan::fromCharString("6H"),  4,
    chip::app::DataModel::List<const SemanticTagType>(sTags, 1) },
};

class HumidifierModesManager : public ModeSel::SupportedModesManager {
public:
  ModeOptionsProvider getModeOptionsProvider(chip::EndpointId) const override {
    return ModeOptionsProvider(sHumidifierModes, sHumidifierModes + 5);
  }
  chip::Protocols::InteractionModel::Status getModeOptionByMode(
      chip::EndpointId, uint8_t mode, const ModeOptionType **dataPtr) const override {
    for (uint8_t i = 0; i < 5; i++) {
      if (sHumidifierModes[i].mode == mode) {
        *dataPtr = &sHumidifierModes[i];
        return chip::Protocols::InteractionModel::Status::Success;
      }
    }
    return chip::Protocols::InteractionModel::Status::InvalidCommand;
  }
};

HumidifierModesManager sHumidifierModesManager;

} // namespace

esp_matter::endpoint_t *create_humidifier_mode_select_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::mode_select_device::config_t config;
  strncpy(config.mode_select.mode_select_description, "Humidifier",
          sizeof(config.mode_select.mode_select_description) - 1);
  config.mode_select.current_mode = 0;
  config.mode_select.delegate     = &sHumidifierModesManager;
  return esp_matter::endpoint::mode_select_device::create(node, &config, 0x00, priv_data);
}

// ---- Generic Switch endpoint factory ----
//
// Generic Switch device type (0x000F) with the Switch cluster (0x003B) configured
// for MomentarySwitch only (feature bit 1). Apple Home will surface this as a
// physical button that automations can trigger on. Without MomentarySwitchRelease,
// only the InitialPress event is emitted; CurrentPosition flips 1 → 0 to represent
// each tap.

esp_matter::endpoint_t *create_humidifier_switch_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::generic_switch::config_t config;
  config.switch_cluster.number_of_positions = 2;
  config.switch_cluster.current_position    = 0;
  esp_matter::endpoint_t *endpoint =
      esp_matter::endpoint::generic_switch::create(node, &config, 0x00, priv_data);
  if (!endpoint) return nullptr;
  esp_matter::cluster_t *cluster =
      esp_matter::cluster::get(endpoint, chip::app::Clusters::Switch::Id);
  if (cluster) {
    esp_matter::cluster::switch_cluster::feature::momentary_switch::add(cluster);
  }
  return endpoint;
}

// ---- ISR latches (one per physical button) ----

static volatile bool s_fan_button_pressed  = false;
static volatile bool s_lamp_button_pressed = false;

static void IRAM_ATTR fan_button_isr_handler(void *)  { s_fan_button_pressed  = true; }
static void IRAM_ATTR lamp_button_isr_handler(void *) { s_lamp_button_pressed = true; }

extern "C" {

void delay_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t matter_fan_update_mode(uint16_t endpoint_id, uint8_t fan_mode) {
  // Update FanMode (0x0000). 
  // esp_matter_attr_val_t val = esp_matter_uint8(fan_mode);
  // return esp_matter::attribute::update(endpoint_id, 0x00000202, 0x00000000, &val);
  // Update FanMode (0x0000)
  esp_matter_attr_val_t val = esp_matter_uint8(fan_mode);
  esp_err_t err = esp_matter::attribute::update(endpoint_id, 0x00000202, 0x00000000, &val);
  if (err != ESP_OK) return err;
  // Also sync PercentSetting (0x0002) so Apple Home slider reflects the state.
  // FanModeSequence is Off/Low/High/Auto; Night has no named slider position
  // so it reports back as High (67%) to keep the slider at a valid anchor.
  // Mapping: Off=0%, Low=30%, Med(Night)=60%, High=100%
  static const uint8_t kPercent[4] = {0, 30, 60, 100};
  val = esp_matter_uint8(fan_mode < 4 ? kPercent[fan_mode] : 0);
  return esp_matter::attribute::update(endpoint_id, 0x00000202, 0x0000000632, &val);
}

esp_err_t matter_onoff_update(uint16_t endpoint_id, bool on) {
  // Update OnOff (0x0000) on the OnOff cluster (0x0006).
  esp_matter_attr_val_t val = esp_matter_bool(on);
  return esp_matter::attribute::update(endpoint_id, 0x00000006, 0x00000000, &val);
}

esp_err_t matter_mode_select_update_current_mode(uint16_t endpoint_id, uint8_t mode) {
  // Update CurrentMode (0x0003) on the Mode Select cluster (0x0050).
  esp_matter_attr_val_t val = esp_matter_uint8(mode);
  return esp_matter::attribute::update(endpoint_id, 0x00000050, 0x00000003, &val);
}

esp_err_t matter_switch_press(uint16_t endpoint_id) {
  // Bump CurrentPosition (0x0001) to 1, fire InitialPress, then back to 0.
  // With MomentarySwitch only (no MomentarySwitchRelease), InitialPress is the
  // only event the cluster emits. Apple Home renders this as a single tap.
  esp_matter_attr_val_t one  = esp_matter_uint8(1);
  esp_matter_attr_val_t zero = esp_matter_uint8(0);
  esp_matter::attribute::update(endpoint_id, 0x0000003B, 0x00000001, &one);
  chip::app::Clusters::SwitchServer::Instance().OnInitialPress(endpoint_id, 1);
  return esp_matter::attribute::update(endpoint_id, 0x0000003B, 0x00000001, &zero);
}

void setup_fan_button_listen_gpio(int32_t gpio_num) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << gpio_num;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_NEGEDGE;
  gpio_config(&cfg);
  gpio_install_isr_service(0);  // installs the service; call this before setup_lamp_button_listen_gpio
  gpio_isr_handler_add(static_cast<gpio_num_t>(gpio_num), fan_button_isr_handler, nullptr);
}

bool matter_fan_button_was_pressed(void) {
  if (s_fan_button_pressed) {
    s_fan_button_pressed = false;
    return true;
  }
  return false;
}

void setup_lamp_button_listen_gpio(int32_t gpio_num) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << gpio_num;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_NEGEDGE;
  gpio_config(&cfg);
  // gpio_install_isr_service already called by setup_fan_button_listen_gpio
  gpio_isr_handler_add(static_cast<gpio_num_t>(gpio_num), lamp_button_isr_handler, nullptr);
}

bool matter_lamp_button_was_pressed(void) {
  if (s_lamp_button_pressed) {
    s_lamp_button_pressed = false;
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
