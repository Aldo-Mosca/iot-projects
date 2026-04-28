// GPIO pin connected to the relay module signal input.
// XIAO ESP32-C6 pin mapping: D3 = GPIO5. Confirm against your wiring before flashing.
let relayGPIO: Int32 = 5

// Controls a 5V active-high relay module via a GPIO output pin.
final class Relay {
  var enabled: Bool = false {
    didSet {
      gpio_set_level(gpio_num_t(rawValue: relayGPIO), enabled ? 1 : 0)
    }
  }

  init() {
    var cfg = gpio_config_t()
    cfg.pin_bit_mask = UInt64(1) << relayGPIO
    cfg.mode = GPIO_MODE_OUTPUT
    cfg.pull_up_en = GPIO_PULLUP_DISABLE
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE
    cfg.intr_type = GPIO_INTR_DISABLE
    gpio_config(&cfg)
    // Start with relay off.
    gpio_set_level(gpio_num_t(rawValue: relayGPIO), 0)
  }
}
