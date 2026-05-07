// Hardware K1 cycle: Off(0) → High(1) → Low(2) → Night(3) → Off(0)
//
// FanModeSequence 0x02 = Off / Low / High  (no Auto, no slider)
// Apple Home sends FanMode (0x0000) writes via discrete mode buttons.
// PercentSetting is null so no speed slider is rendered.
//
// FanMode ↔ hardware state:
//   Off (0)  → hw Off(0)
//   Low (1)  → hw Low(1)
//   Medium (2) → hw Medium(2)
//   High (3) → hw High(3)
//
// Night (hw 1) is unreachable from Apple Home — physical button only.
// When Night is active, Matter reports FanMode Low (1) as the closest anchor.

let modeForHardwareState: [UInt8] = [0, 1, 2, 3]  // hw index → FanMode value
// hw Off(0)→0, hw Low(1)→1, hw Med(2)→2, hw High(3)→3

func hardwareStateForFanMode(_ mode: UInt8) -> UInt8 {
  switch mode {
  case 0:  return 0  // Off
  case 1:  return 1  // Low
  case 2:  return 2  // Medium
  case 3:  return 3  // High
  default: return 0  // ignore unknown modes
  }
}

func hardwareStateForPercent(_ percent: UInt8) -> UInt8 {
  switch percent {
  case 0:       return 0  // Off
  case 1...30:  return 1  // Low
  case 31...60: return 2  // Medium
  default:      return 3  // High (51–100)
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
  // TODO find the XML where attributes are defined and add this:
  // fanEndpoint.fanModeSequence = 0     // Off/Low/Med/High, with low reserved for "night" mode
  fanEndpoint.eventHandler = { event in
    switch event.attribute {
      case .fanMode:
          targetHw = hardwareStateForFanMode(UInt8(event.value & 0xFF))
          print("🧶 🧶 🧶 🧶 🧶 🧶 🧶 🧶 FanMode received \(event.value)")
      case .percentSetting:
          targetHw = hardwareStateForPercent(UInt8(event.value & 0xFF))
          print("🧵 🧵 🧵 🧵 🧵 🧵 🧵 🧵 Percent setting received \(event.value)")
      default:
          print("SHOULDN'T BE HERE 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 ") 
          return
    }
    // let presses = (Int(targetHw) - Int(hwState) + 4) % 4
    // for i in 0..<presses {
    //   if i > 0 { delay_ms(200) }
    //   fanButton.press()
    // }
    fanButton.press()
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
      print("button 1 pressed yay 👍 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏 👏👏👏  ")
      hwState = (hwState + 1) % 4
      print("hwState is: \(hwState)")
      fanEndpoint.updateFanMode(modeForHardwareState[Int(hwState)])
      fanButton.press() // Simulate the button press on the hardware 
    }
    if matter_lamp_button_was_pressed() {
      print("button 2 pressed yay 👍 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾 👾👾👾  ")
      lampIsOn = !lampIsOn
      lightEndpoint.update(lampIsOn)
      lampButton.press()  // Simulate the button press on the hardware 
    }
    delay_ms(200)
  }
}
