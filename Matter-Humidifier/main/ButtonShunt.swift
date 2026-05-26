// K1 = lamp button.  K2 = fan mode button.

// GPIO shunted across K1 contacts (output, open-drain) — simulates lamp button press.
// TODO: confirm pin — XIAO ESP32-C6 D0 = GPIO0
let lampButtonGPIO: Int32 = 0

// GPIO wired to K1 line as passive input — detects physical lamp button presses.
// TODO: confirm pin — XIAO ESP32-C6 D1 = GPIO1
let lampListenGPIO: Int32 = 1

// GPIO shunted across K2 contacts (output, open-drain) — simulates fan mode button press.
// TODO: confirm pin — XIAO ESP32-C6 D2 = GPIO2
let fanButtonGPIO: Int32 = 2

// GPIO wired to K2 line as passive input — detects physical fan mode button presses.
// TODO: confirm pin — XIAO ESP32-C6 D3 = GPI21
let fanListenGPIO: Int32 = 21      // WAS 3

// Simulates a momentary button press by briefly pulling a GPIO low in open-drain mode.
// Open-drain means: write 0 → actively pulls low (press); write 1 → high-impedance (release).
// This is safe to connect directly across button contacts without fighting any pull-up on the green board.
final class ButtonShunt {
  let gpio: Int32

  init(gpio: Int32) {
    self.gpio = gpio
    var cfg = gpio_config_t()
    cfg.pin_bit_mask = UInt64(1) << gpio
    cfg.mode = GPIO_MODE_OUTPUT
    cfg.pull_up_en = GPIO_PULLUP_DISABLE
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE
    cfg.intr_type = GPIO_INTR_DISABLE
    gpio_config(&cfg)
    gpio_set_level(gpio_num_t(rawValue: gpio), 1)  // start released
  }

  // // DEBUG: Temporary: blink the pin so you can confirm it's driving correctly
  //   print("DEBUG START 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 💃🕺 ")
  //   for _ in 0..<5 {
  //       gpio_set_level(gpio_num_t(rawValue: gpio), 1)
  //       vTaskDelay(50)
  //       gpio_set_level(gpio_num_t(rawValue: gpio), 0)
  //       vTaskDelay(50)
  //   }
  //   gpio_set_level(gpio_num_t(rawValue: gpio), 1)  // back to idle high
  //   print("DEBUG END 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫 🍄‍🟫  ")
  //   // END DEBUG
  // }

  func press(durationMs: UInt32 = 100) {
    gpio_set_level(gpio_num_t(rawValue: gpio), 0)  // pull low — press
    print("[HUMI] 🚩 🚩 🚩 🚩 🚩 BUTTON PRESS \(gpio) 🚩 🚩 🚩 🚩 🚩 🚩 🚩 🚩 🚩 ")
    delay_ms(durationMs)
    gpio_set_level(gpio_num_t(rawValue: gpio), 1)  // release
    print("[HUMI] 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 ")
  }
}
