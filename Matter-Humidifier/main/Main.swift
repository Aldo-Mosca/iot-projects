// Hardware K1 cycle: Off(0) → High(1) → Low(2) → Night(3) → Off(0)
//
// FanModeSequence 0x02 = Off / Low / High  (no Auto, no slider)
// Apple Home sends FanMode (0x0000) writes via discrete mode buttons.
// PercentSetting is null so no speed slider is rendered.
//
// FanMode ↔ hardware state:
//   Off (0)  → hw Off(0)
//   Low (1)  → hw Low(2)
//   High (3) → hw High(1)
//
// Night (hw 3) is unreachable from Apple Home — physical button only.
// When Night is active, Matter reports FanMode Low (1) as the closest anchor.

let modeForHardwareState: [UInt8] = [0, 3, 1, 1]  // hw index → FanMode value
// hw Off(0)→0, hw High(1)→3, hw Low(2)→1, hw Night(3)→1

func hardwareStateForFanMode(_ mode: UInt8) -> UInt8 {
  switch mode {
  case 0:  return 0  // Off
  case 1:  return 2  // Low
  case 3:  return 1  // High
  default: return 0  // ignore unknown modes
  }
}

func hardwareStateForPercent(_ percent: UInt8) -> UInt8 {
  switch percent {
  case 0:      return 0  // Off
  case 1...50: return 2  // Low
  default:     return 1  // High (51–100)
  }
}

@_cdecl("app_main")
func main() {
  var targetHw: UInt8 = 0

  print("Hello, Embedded Swift! (Humidifier / Fan device) 🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰")

  let fanButton  = ButtonShunt(gpio: fanButtonGPIO)
  setup_fan_button_listen_gpio(fanListenGPIO)

  let lampButton = ButtonShunt(gpio: lampButtonGPIO)
  setup_lamp_button_listen_gpio(lampListenGPIO)

  // Assumed hardware states on boot.
  var hwState: UInt8 = 0   // fan: Off
  var lampIsOn: Bool = false

  // (1) Create a Matter root node
  let rootNode = Matter.Node()
  rootNode.identifyHandler = { print("identify") }

  // (2) Create a Fan endpoint (device type 0x0044)
  let fanEndpoint = Matter.Fan(node: rootNode)
  fanEndpoint.eventHandler = { event in
    switch event.attribute {
      case .fanMode:
          targetHw = hardwareStateForFanMode(UInt8(event.value & 0xFF))
      case .percentSetting:
          targetHw = hardwareStateForPercent(UInt8(event.value & 0xFF))
      default:
          return
    }
    let presses = (Int(targetHw) - Int(hwState) + 4) % 4
    for i in 0..<presses {
      if i > 0 { delay_ms(200) }
      fanButton.press()
    }
    hwState = targetHw
  }

  // (2.5) Create an OnOff Light endpoint for the lamp (K1 button)
  let lightEndpoint = Matter.OnOffLight(node: rootNode)
  lightEndpoint.eventHandler = { event in
    guard case .onOff = event.attribute else { return }
    let targetOn = event.value != 0
    if targetOn != lampIsOn {
      lampButton.press()
      lampIsOn = targetOn
    }
  }

  // (3) Add the endpoints to the node
  rootNode.addEndpoint(lightEndpoint)
  rootNode.addEndpoint(fanEndpoint)

  // (4) Start Matter
  let app = Matter.Application()
  app.rootNode = rootNode
  app.start()

  // Main loop: poll physical buttons and sync state back to Matter.
  // Keep local variables alive — workaround for swift-matter-examples issue #10.
  while true {
    if matter_fan_button_was_pressed() {
      print("button pressed yay 👍 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏  ")
      hwState = (hwState + 1) % 4
      print("hwState is: \(hwState)")
      fanEndpoint.updateFanMode(modeForHardwareState[Int(hwState)])
    }
    if matter_lamp_button_was_pressed() {
      lampIsOn = !lampIsOn
      lightEndpoint.update(lampIsOn)
    }
    delay_ms(200)
  }
}
