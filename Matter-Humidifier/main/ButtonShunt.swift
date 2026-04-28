// GPIO pin connected to the K1 button contacts (output, open-drain — simulates press).
// Wire between this XIAO pin and one leg of K1 (other leg to GND on the green board).
// TODO: always confirm pin — XIAO ESP32-C6 D0 = GPI00
let powerButtonGPIO: Int32 = 0

// GPIO wired to the K1 button line as a passive input to detect physical button presses.
// Must be a different pin from powerButtonGPIO.
// TODO: always confirm pin — XIAO ESP32-C6 D1 = GPI01 
let buttonListenGPIO: Int32 = 1

// Simulates a momentary button press by briefly pulling a GPIO low in open-drain mode.
// Open-drain means: write 0 → actively pulls low (press); write 1 → high-impedance (release).
// This is safe to connect directly across button contacts without fighting any pull-up on the green board.
final class ButtonShunt {
  let gpio: Int32

  init(gpio: Int32) {
    self.gpio = gpio
    var cfg = gpio_config_t()
    cfg.pin_bit_mask = UInt64(1) << gpio
    cfg.mode = GPIO_MODE_OUTPUT_OD
    cfg.pull_up_en = GPIO_PULLUP_DISABLE
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE
    cfg.intr_type = GPIO_INTR_DISABLE
    gpio_config(&cfg)
    gpio_set_level(gpio_num_t(rawValue: gpio), 1)  // start released
  }

  func press(durationMs: UInt32 = 100) {
    gpio_set_level(gpio_num_t(rawValue: gpio), 0)  // pull low — press
    delay_ms(durationMs)
    gpio_set_level(gpio_num_t(rawValue: gpio), 1)  // release
  }
}
