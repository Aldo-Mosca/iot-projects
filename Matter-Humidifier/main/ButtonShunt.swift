// K1 = lamp button.  K2 = fan mode button.
// (Original naming preserved; new panel uses S2=LIGHT and S1=MIST labels —
// see project memory / DEVLOG. fanButton variables drive the MIST side,
// lampButton variables drive the LIGHT side.)

// ===== Output pins (button shunts via N-channel MOSFET) =====

// GPIO driving MOSFET gate that shorts S2 LIGHT (K1) contacts when HIGH.
// XIAO ESP32-C6: D0 = GPIO0. (D0 is boot/flash-related per Seeed forum; safe
// as an OUTPUT driving a high-impedance MOSFET gate — gate is floating at
// boot before app_main configures the pin.)
let lampButtonGPIO: Int32 = 0

// GPIO driving MOSFET gate that shorts S1 MIST (K2) contacts when HIGH.
// XIAO ESP32-C6: D2 = GPIO2. Same caveat as D0.
let fanButtonGPIO: Int32 = 2

// ===== Panel sensing inputs (via JST PH002 splice cable) =====
// See docs/WIRING-UPDATE.md for the cable pin map and divider schematic.
// All inputs placed on D5–D10 (GPIO5–GPIO10) per Seeed-forum guidance — D0–D4
// are unsafe for I/O on the XIAO ESP32-C6 (boot strapping / SPI flash).
// D8/D9 (GPIO8/GPIO9) are also avoided here because they're ESP32-C6
// strapping pins (BOOT button on GPIO9; JTAG mux on GPIO8) and can prevent
// boot if pulled LOW by the panel at startup.

// ===== Panel sensing inputs (via JST PH002 splice cable) =====
// See docs/WIRING-UPDATE.md for the cable pin map and divider schematic.
// Three digital inputs placed on D5/D7/D10 (safe per Seeed forum: D5–D10
// are general-I/O-safe). The one ADC input (L2) has to go on D1 because
// although D0–D4 are flagged as boot/flash-related and generally unsafe,
// only D0/D1/D2 are routed out as ADC pins on this board and D0/D2 are
// already driving the MOSFET shunts.

// L1 — LIGHT button line. Active LOW when pressed; idle HIGH (~5 V from panel
// pull-up). Read through a 100k/100k divider; ESP sees ~2.5V idle, ~0V on press.
// XIAO ESP32-C6: D7 = GPIO17. (GPIO17 is the chip's default UART0 RX, but the
// XIAO uses USB-Serial-JTAG for `idf.py monitor`, so this is harmless.)
let lightButtonInputGPIO: Int32 = 17

// L2 — MIST mode row selector. Three discrete voltage levels on the panel cable:
//   ~0.0 V  → device off (state 0)
//   ~2.2 V  → modes 3H / 6H  (states 3, 4)
//   ~3.1 V  → modes On / 1H  (states 1, 2)
// Read via ADC1_CH1 through a 100k/100k divider (halves all the above).
// XIAO ESP32-C6: D1 = GPIO1. The only ADC pin available — D0/D2 are spoken
// for by the MOSFET shunts, and other ADC1 channels (CH3–CH6) aren't routed
// out as ADC pins on this board even though the underlying GPIOs exist.
let mistRowGPIO: Int32 = 1

// L3 — MIST column A. Digital; LOW when active. Lit in modes On (1) and 3H (3).
// Read through a 100k/100k divider, plain digital input.
// XIAO ESP32-C6: D10 = GPIO18.
let mistColAGPIO: Int32 = 18

// L4 — MIST column B. Digital; LOW when active. Lit in modes 1H (2) and 6H (4).
// XIAO ESP32-C6: D5 = GPIO23.
let mistColBGPIO: Int32 = 23

// ===== Legacy listen pins (superseded by panel sensing above) =====
// Old K1/K2 listen wiring proved noise-prone and unreliable on the new green
// board. Replaced by panel-LED state sensing on L2/L3/L4 (mist) and a clean
// button-line read on L1 (light). The shunt outputs (lampButtonGPIO,
// fanButtonGPIO) above are unchanged.
//
// NOTE: XIAO ESP32-C6 D-numbers do NOT map 1:1 to GPIO numbers in general
// (only D0/D1/D2 happen to match). D5=GPIO23, D7=GPIO17, D10=GPIO18, etc.
// Always cross-reference Seeed's datasheet's silkscreen-to-GPIO table.
//
// let lampListenGPIO: Int32 = 1    // was D1 = GPIO1
// let fanListenGPIO: Int32 = 21    // was wrongly written as 21 (and earlier
//                                  // as 3); pin unused under the new sensing
//                                  // approach.

// Simulates a momentary button press by briefly driving a GPIO high.
// GPIO drives an N-channel MOSFET gate: HIGH → MOSFET conducts → shorts the
// button contacts (press); LOW → MOSFET off → contacts open (released/idle).
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
    gpio_set_level(gpio_num_t(rawValue: gpio), 0)  // start released (MOSFET off)
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

  func press(durationMs: UInt32 = 600) {
    gpio_set_level(gpio_num_t(rawValue: gpio), 1)  // drive high — MOSFET on, press
    print("[HUMI] 🚩 🚩 🚩 🚩 🚩 BUTTON PRESS \(gpio) 🚩 🚩 🚩 🚩 🚩 🚩 🚩 🚩 🚩 ")
    delay_ms(durationMs)
    gpio_set_level(gpio_num_t(rawValue: gpio), 0)  // drive low — MOSFET off, release
    print("[HUMI] 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 🦄 ")
  }
}
