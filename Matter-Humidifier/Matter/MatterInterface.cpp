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
#include "hal/gpio_types.h"
#include <app/clusters/mode-select-server/supported-modes-manager.h>
#include <app/clusters/switch-server/switch-server.h>
#include <cstring>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

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

// ---- Air Purifier endpoint factory ----
// (Definition moved below the anonymous namespace that holds
// sHumidifierModesManager — the factory needs to reference it.)

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

// Helper: set the StandardNamespace (0x0001) attribute on a Mode Select
// cluster. The mode_select::config_t.standard_namespace member is declared
// const, so it can't be set via the config struct — we have to write the
// attribute storage directly after the cluster has been created.
//
// Apple Home appears to gate rendering of custom ModeSelect mode lists on
// the StandardNamespace being non-null (and possibly on it being a value
// Home recognizes). Without this, Home reads FeatureMap / ClusterRevision /
// Description / CurrentMode but skips SupportedModes entirely.
static void set_mode_select_standard_namespace(
    esp_matter::endpoint_t *endpoint, uint16_t ns) {
  esp_matter::cluster_t *cluster =
      esp_matter::cluster::get(endpoint, 0x00000050);  // ModeSelect cluster
  if (!cluster) return;
  esp_matter::attribute_t *attr =
      esp_matter::attribute::get(cluster, 0x00000001); // StandardNamespace
  if (!attr) return;
  esp_matter_attr_val_t val =
      esp_matter_nullable_uint16(nullable<uint16_t>(ns));
  esp_matter::attribute::set_val(attr, &val);
}

// ---- Air Purifier endpoint factory ----
//
// Device type 0x002D. Spec-mandatory clusters: Identify, Groups, FanControl
// (provided by the air_purifier::create() factory). Additional clusters
// added on top: OnOff (for a simple on/off toggle in Apple Home) and
// Mode Select (for the 5-mode Off/On/1H/3H/6H humidifier mist cycle).
// Filter monitoring clusters (HepaFilter 0x0071, ActivatedCarbon 0x0072)
// are spec-optional and not added here.

esp_matter::endpoint_t *create_humidifier_air_purifier_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::air_purifier::config_t config;
  config.fan_control.fan_mode          = 0;       // Off
  config.fan_control.fan_mode_sequence = 0x03;    // Off/Low/High/Auto
  config.fan_control.percent_setting   = static_cast<uint8_t>(0);
  config.fan_control.percent_current   = 0;
  esp_matter::endpoint_t *endpoint =
      esp_matter::endpoint::air_purifier::create(node, &config, 0x00, priv_data);
  if (!endpoint) return nullptr;

  // Extra cluster: OnOff (no Lighting feature — this is on an air-purifier
  // endpoint, not a light).
  esp_matter::cluster::on_off::config_t onoff_cfg;
  onoff_cfg.on_off = false;
  esp_matter::cluster::on_off::create(endpoint, &onoff_cfg,
      esp_matter::CLUSTER_FLAG_SERVER, ESP_MATTER_NONE_FEATURE_ID);

  // Extra cluster: Mode Select (shares the same SupportedModesManager
  // delegate used by the standalone mode_select endpoint below).
  esp_matter::cluster::mode_select::config_t ms_cfg;
  strncpy(ms_cfg.mode_select_description, "Humidifier",
          sizeof(ms_cfg.mode_select_description) - 1);
  ms_cfg.current_mode = 0;
  ms_cfg.delegate     = &sHumidifierModesManager;
  esp_matter::cluster::mode_select::create(endpoint, &ms_cfg,
      esp_matter::CLUSTER_FLAG_SERVER, ESP_MATTER_NONE_FEATURE_ID);

  // Coax Apple Home into rendering the custom modes — see helper comment.
  // 0x0040 = Common Mode Namespace (Off / On / Auto / Boost / Night / Sleep
  // and friends). Recognized by Apple Home; namespace value 0 was not.
  set_mode_select_standard_namespace(endpoint, 0x0040);

  return endpoint;
}

esp_matter::endpoint_t *create_humidifier_mode_select_endpoint(
    esp_matter::node_t *node, void *priv_data) {
  esp_matter::endpoint::mode_select_device::config_t config;
  strncpy(config.mode_select.mode_select_description, "Humidifier",
          sizeof(config.mode_select.mode_select_description) - 1);
  config.mode_select.current_mode = 0;
  config.mode_select.delegate     = &sHumidifierModesManager;
  esp_matter::endpoint_t *endpoint =
      esp_matter::endpoint::mode_select_device::create(node, &config, 0x00, priv_data);
  if (endpoint) {
    set_mode_select_standard_namespace(endpoint, 0x0040);
  }
  return endpoint;
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

// ===== GPIO signal scanner (startup diagnostic) =====
// Scans candidate GPIOs for the 100 Hz / 33 µs S2 signal. Logs HIGHs per
// GPIO so we can identify which physical pin the divider output landed on.

void matter_gpio_scan_for_signal(void) {
  static const int kCandidates[] = {3,4,5,6,7,8,9,10,11,14,15,16,17,19,20,21,22};
  ESP_LOGI("[HUMI]", "GPIO signal scan starting (skipping 0,1,2,18,23)...");
  for (int ci = 0; ci < (int)(sizeof(kCandidates)/sizeof(kCandidates[0])); ci++) {
    int g = kCandidates[ci];
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << g;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    int highs = 0;
    for (int i = 0; i < 200000; i++) {
      if (gpio_get_level(static_cast<gpio_num_t>(g))) highs++;
    }
    ESP_LOGI("[HUMI]", "  GPIO%02d: %d/200000 HIGHs", g, highs);
  }
  ESP_LOGI("[HUMI]", "GPIO signal scan done. Expect ~600 HIGHs on the live pin.");
}

// ===== S2 (LIGHT) scan-line pulse detection =====
// The humidifier MCU emits 100 Hz / ~33 µs rising pulses on the S2 line during
// idle. A physical S2 press suppresses the pulses entirely (line held LOW).
// Detection: rising-edge ISR stores the tick of each pulse; the main loop checks
// how long ago the last pulse arrived — > 50 ms means the button is pressed.

static volatile uint32_t s_lamp_pulse_count = 0;

static void IRAM_ATTR lamp_listen_isr_handler(void *) {
  s_lamp_pulse_count++;
}

extern "C" {

void setup_lamp_listen_gpio(int32_t gpio_num) {
  // Install ISR service first — before any interrupt is enabled — so no edge
  // can fire into an unregistered handler and latch the status register.
  gpio_install_isr_service(0);  // idempotent if already installed

  // Configure pin as plain input with interrupt disabled for now.
  gpio_config_t cfg = {};
  cfg.pin_bit_mask   = 1ULL << gpio_num;
  cfg.mode           = GPIO_MODE_INPUT;
  cfg.pull_up_en     = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en   = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type      = GPIO_INTR_DISABLE;
  gpio_config(&cfg);

  // Register handler, then enable edge detection — handler is ready before
  // any edge can fire.
  esp_err_t err = gpio_isr_handler_add(static_cast<gpio_num_t>(gpio_num), lamp_listen_isr_handler, nullptr);
  if (err != ESP_OK) ESP_LOGE("[HUMI]", "gpio_isr_handler_add failed: %d", err);
  gpio_set_intr_type(static_cast<gpio_num_t>(gpio_num), GPIO_INTR_ANYEDGE);
  gpio_intr_enable(static_cast<gpio_num_t>(gpio_num));
}

uint32_t matter_lamp_pulse_count(void) {
  return s_lamp_pulse_count;
}

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

// ===== MIST panel LED sensing =====
// Replaces the K2 listen-ISR approach (which was unreliable on the new green
// board). Polls three panel-cable lines on each main-loop tick:
//   L2 (row, ADC)  — 3-level voltage selects "off" / "pair 1" / "pair 2"
//   L3 (col A, digital) — distinguishes On/3H from 1H/6H
//   L4 (col B, digital)
// See docs/WIRING-UPDATE.md for the divider schematic and pin map.

static adc_oneshot_unit_handle_t s_mist_adc_handle = nullptr;
static adc_cali_handle_t s_mist_adc_cali_handle = nullptr;
static adc_channel_t s_mist_row_channel = ADC_CHANNEL_0;
static int32_t s_mist_col_a_gpio = -1;
static int32_t s_mist_col_b_gpio = -1;

void setup_mist_panel_sensors(int32_t row_gpio, int32_t col_a_gpio, int32_t col_b_gpio) {
  s_mist_col_a_gpio = col_a_gpio;
  s_mist_col_b_gpio = col_b_gpio;

  // ESP32-C6 ADC1 channels map 1:1 with GPIO0..GPIO6. Only D0/D1/D2 are
  // routed out on the XIAO ESP32-C6 — and D0/D2 are spoken for by the button
  // shunts, leaving D1 (GPIO1) as the only viable ADC pin.
  if (row_gpio < 0 || row_gpio > 6) {
    ESP_LOGE("[HUMI]", "MIST row GPIO %d is not ADC1-capable on ESP32-C6", (int)row_gpio);
    return;
  }
  s_mist_row_channel = static_cast<adc_channel_t>(row_gpio);

  adc_oneshot_unit_init_cfg_t init_cfg = {};
  init_cfg.unit_id = ADC_UNIT_1;
  init_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
  if (adc_oneshot_new_unit(&init_cfg, &s_mist_adc_handle) != ESP_OK) {
    ESP_LOGE("[HUMI]", "adc_oneshot_new_unit failed");
    s_mist_adc_handle = nullptr;
    return;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten = ADC_ATTEN_DB_12;   // ~0..3.3 V usable input range
  chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  adc_oneshot_config_channel(s_mist_adc_handle, s_mist_row_channel, &chan_cfg);

  // Curve-fitting calibration — gives us mV instead of raw counts.
  // Falls back gracefully if the chip doesn't support it.
  adc_cali_curve_fitting_config_t cali_cfg = {};
  cali_cfg.unit_id = ADC_UNIT_1;
  cali_cfg.chan = s_mist_row_channel;
  cali_cfg.atten = ADC_ATTEN_DB_12;
  cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_mist_adc_cali_handle) != ESP_OK) {
    ESP_LOGW("[HUMI]", "ADC calibration unavailable, will use raw ADC counts");
    s_mist_adc_cali_handle = nullptr;
  }

  // Col A / Col B: plain digital inputs, no pull (the panel drives them
  // rail-to-rail via the 100k/100k divider).
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << col_a_gpio) | (1ULL << col_b_gpio);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;   // polled, not interrupt-driven
  gpio_config(&cfg);
}

uint8_t matter_read_mist_state(void) {
  if (!s_mist_adc_handle) return 0;

  // Average a few ADC samples to reject single-sample noise on the row line.
  const int kSamples = 8;
  int raw_sum = 0;
  for (int i = 0; i < kSamples; i++) {
    int raw;
    if (adc_oneshot_read(s_mist_adc_handle, s_mist_row_channel, &raw) != ESP_OK) {
      return 0;
    }
    raw_sum += raw;
  }
  int raw_avg = raw_sum / kSamples;

  int mv = raw_avg;
  if (s_mist_adc_cali_handle) {
    adc_cali_raw_to_voltage(s_mist_adc_cali_handle, raw_avg, &mv);
  }

  bool col_a_lo = (gpio_get_level(static_cast<gpio_num_t>(s_mist_col_a_gpio)) == 0);
  bool col_b_lo = (gpio_get_level(static_cast<gpio_num_t>(s_mist_col_b_gpio)) == 0);

  // Voltages at the ESP input (after the 100k/100k divider halves the panel
  // signal):
  //   Off:                 ~5  mV   (both cols also LOW — panel drives no LED)
  //   Pair 2 (3H / 6H):   ~1100 mV   (panel ~2.2 V)
  //   Pair 1 (On / 1H):   ~1550 mV   (panel ~3.1 V)
  // Thresholds picked at the midpoints, with generous off-detection floor.
  //
  // Off-state cross-check: in a real "on" mode, exactly one of L3/L4 is LOW
  // (the active LED). Both LOW is the off-state pattern, which the panel also
  // produces when the device is genuinely off. So if both columns are LOW we
  // treat the read as off, regardless of what L2 reports — this rejects
  // periodic L2 transients (the panel does a scan/refresh every ~10 s that
  // briefly lifts L2 to ~550 mV without disturbing the columns).
  uint8_t decoded;
  bool exactly_one_col_active = (col_a_lo != col_b_lo);
  if (mv < 200 || !exactly_one_col_active) {
    decoded = 0;                                 // Off (or transient on L2 alone)
  } else if (mv > 1300) {
    // Pair 1: On (1) or 1H (2)
    decoded = col_a_lo ? 1 : 2;
  } else {
    // Pair 2: 3H (3) or 6H (4)
    decoded = col_a_lo ? 3 : 4;
  }

  // [DEBUG] Verbose per-poll trace. Re-enable when diagnosing sensing issues.
  // printf("[HUMI] 🔬 mist read: raw=%d mv=%d col_a=%d col_b=%d -> state %u\n",
  //        raw_avg, mv, col_a_lo ? 1 : 0, col_b_lo ? 1 : 0, decoded);

  return decoded;
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
