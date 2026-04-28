// Hardware K1 cycle: Off(0) → High(1) → Low(2) → Night(3) → Off(0)
//
// Apple Home sends PercentSetting (slider) writes; FanMode is never used for UI.
//
// FanModeSequence 0x03 = Off / Low / High / Auto
//
// PercentSetting ↔ hardware state:
//   0%    → hw Off(0)    FanMode Off(0)
//   33%   → hw Low(2)    FanMode Low(1)
//   67%   → hw High(1)   FanMode High(3)
//   67%   → hw Night(3)  FanMode Med(2)  ← Night has no named slider position;
//                                           reports back as High so slider stays
//                                           at a valid anchor. Physical button only.
//
// Slider ranges → hardware (incoming from Apple Home):
//   0     → Off
//   1–50  → Low
//   51+   → High   (Night not reachable from slider)

let modeForHardwareState: [UInt8] = [0, 3, 1, 2]  // hw index → FanMode value

func hardwareStateForPercent(_ percent: UInt8) -> UInt8 {
  switch percent {
  case 0:      return 0  // Off
  case 1...50: return 2  // Low
  default:     return 1  // High (51–100)
  }
}

@_cdecl("app_main")
func main() {
  print("Hello, Embedded Swift! (Humidifier / Fan device)")

  let powerButton = ButtonShunt(gpio: powerButtonGPIO)
  setup_button_listen_gpio(buttonListenGPIO)

  // Assumed hardware state. On boot the humidifier is assumed Off.
  var hwState: UInt8 = 0

  // (1) Create a Matter root node
  let rootNode = Matter.Node()
  rootNode.identifyHandler = { print("identify") }

  // (2) Create a Fan endpoint (device type 0x0044)
  let fanEndpoint = Matter.Fan(node: rootNode)
  fanEndpoint.eventHandler = { event in
    // Apple Home sends PercentSetting writes; ignore everything else.
    guard case .percentSetting = event.attribute else { return }
    let percent = UInt8(event.value & 0xFF)
    let targetHw = hardwareStateForPercent(percent)
    let presses = (Int(targetHw) - Int(hwState) + 4) % 4
    for i in 0..<presses {
      if i > 0 { delay_ms(200) }
      powerButton.press()
    }
    hwState = targetHw
  }

  // (3) Add the endpoint to the node
  rootNode.addEndpoint(fanEndpoint)

  // (4) Start Matter
  let app = Matter.Application()
  app.rootNode = rootNode
  app.start()

  // Main loop: poll for physical K1 presses and sync state back to Matter.
  // matter_fan_update_mode() updates both FanMode and PercentSetting so the
  // Apple Home slider reflects the physical change.
  // Keep local variables alive — workaround for swift-matter-examples issue #10.
  while true {
    if matter_button_was_pressed() {
      hwState = (hwState + 1) % 4
      fanEndpoint.updateFanMode(modeForHardwareState[Int(hwState)])
    }
    delay_ms(200)
  }
}
