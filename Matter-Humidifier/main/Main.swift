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

let modeForHardwareState: [UInt8] = [0, 1, 2, 3, 4]  // hw index → FanMode value -- Straight mapping, but could change in the future
// hw Off(0)→0, hw Low(1)→1, hw High(2)→2, hw Night(3)→3
func hardwareStateForFanMode(_ mode: UInt8) -> UInt8 {
  switch mode {
  case 0:  return 0  // Off
  case 1:  return 1  // On
  case 2:  return 2  // 1H
  case 3:  return 3  // 3H
  case 4:  return 4  // 6H
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

  print("[HUMI] Hello, Embedded Swift! (Humidifier / Fan device) 🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰")

  let fanButton  = ButtonShunt(gpio: fanButtonGPIO)
  // Old K2/MIST listen-ISR approach — replaced by panel-LED state sensing.
  // setup_fan_button_listen_gpio(fanListenGPIO)
  setup_mist_panel_sensors(mistRowGPIO, mistColAGPIO, mistColBGPIO)

  let lampButton = ButtonShunt(gpio: lampButtonGPIO)
  // LIGHT button is still edge-detected via the panel cable's L1 line.
  // Repurposes the existing setup_lamp_button_listen_gpio shim with the new
  // input pin (D3/GPIO21, was the K2 listen wire).
  setup_lamp_button_listen_gpio(lightButtonInputGPIO)

  // Assumed hardware states on boot.
  var hwState: UInt8 = 0   // fan: Off
  var lampIsOn: Bool = false

  // (1) Create a Matter root node
  let rootNode = Matter.Node()
  rootNode.identifyHandler = { print("[HUMI] I am a humidifier") }

  // (2) Create a Fan endpoint (device type 0x0044)
  // let fanEndpoint = Matter.Fan(node: rootNode)
  // fanEndpoint.eventHandler = { event in
  //   switch event.attribute {
  //     case .fanMode:
  //         targetHw = hardwareStateForFanMode(UInt8(event.value & 0xFF))
  //         print("[HUMI] 🧶 🧶 🧶 🧶 🧶 🧶 🧶 🧶 FanMode received \(event.value)")
  //     case .percentSetting:
  //         targetHw = hardwareStateForPercent(UInt8(event.value & 0xFF))
  //         print("[HUMI] 🧵 🧵 🧵 🧵 🧵 🧵 🧵 🧵 Percent setting received \(event.value)")
  //     default:
  //         print("[HUMI] SHOULDN'T BE HERE 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 🥸 ")
  //         return
  //   }
  //   // let presses = (Int(targetHw) - Int(hwState) + 5) % 5
  //   // for i in 0..<presses {
  //   //   if i > 0 { delay_ms(200) }
  //   //   fanButton.press()
  //   // }
  //   fanButton.press()
  //   hwState = targetHw
  // }

  // (2.1) Air Purifier endpoint (device type 0x002D). Uses the same FanControl
  // cluster as the Fan endpoint above — only the device-type ID differs.
  // Apple Home renders this as an air-purifier accessory.
  // let airPurifierEndpoint = Matter.AirPurifier(node: rootNode)
  // airPurifierEndpoint.eventHandler = { event in
  //   switch event.attribute {
  //     case .fanMode:
  //         targetHw = hardwareStateForFanMode(UInt8(event.value & 0xFF))
  //         print("[HUMI] 🌬️ AirPurifier FanMode received \(event.value)")
  //     case .percentSetting:
  //         targetHw = hardwareStateForPercent(UInt8(event.value & 0xFF))
  //         print("[HUMI] 🌬️ AirPurifier PercentSetting received \(event.value)")
  //     default:
  //         return
  //   }
  //   fanButton.press()
  //   hwState = targetHw
  // }

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

  // (2.55) Generic Switch endpoint exposing the physical lamp button as a
  // momentary switch. Read-only from Apple Home — automations can trigger on
  // InitialPress events fired below in the main loop.
  // let switchEndpoint = Matter.GenericSwitch(node: rootNode)

  // (2.6) Create a Mode Select endpoint exposing Off/On/2H/3H/6H.
  // Mode values match hwState directly (Off=0, High=1, Low=2, Night=3),
  // so a write to CurrentMode is handled the same way as a Fan write.
  // Note: Apple Home is not rendering controls for the Mode Select endpoint. 
  // FIXME: A user write on either path will pulse the K1 button — duplicate writes 
  // from both endpoints in quick succession are not currently de-duplicated.
  
  // (2.6) Standalone Mode Select endpoint (device type 0x0027).
  // Re-enabled to expose the custom Off/On/1H/3H/6H modes to Apple Home
  // as a separate tile, since the Air Purifier endpoint's ModeSelect
  // cluster is not rendered by Home (Home only surfaces FanControl on
  // Air Purifier endpoints). The two ModeSelect clusters share the same
  // global SupportedModesManager delegate so the mode lists are identical.
  // let modeSelectEndpoint = Matter.ModeSelectDevice(node: rootNode)
  // modeSelectEndpoint.eventHandler = { event in
  //   guard case .currentMode = event.attribute else { return }
  //   let targetMode = UInt8(event.value & 0xFF)
  //   print("[HUMI] 🪀 CurrentMode received target=\(event.value) hwState=\(hwState)")
  //   let presses = (Int(targetMode) - Int(hwState) + 5) % 5
  //   print("[HUMI] 🪀 computed presses=\(presses)")
  //   for i in 0..<presses {
  //     if i > 0 { delay_ms(200) }
  //     fanButton.press()
  //   }
  //   hwState = targetMode
  // }
  // TEMPORARY KLUDGE; use an onOff light to at least have a UI element in the home app capable of driving the mist button.
  // Renders in Apple Home as a round light icon — single press per Home toggle.
  // hwState/mistIsOn drift after multiple physical presses is a known UX limitation
  // (see commented Air Purifier / Mode Select alternatives below for richer options).
  let mistEndpoint = Matter.OnOffLight(node: rootNode)
  var mistIsOn: Bool = false
  mistEndpoint.eventHandler = { event in
    guard case .onOff = event.attribute else { return }
    let targetOn = event.value != 0
    if targetOn != mistIsOn {
      fanButton.press()
      mistIsOn = targetOn
    }
  }

  // Air Purifier endpoint (device type 0x002D) with three clusters on one
  // endpoint: FanControl (mandatory for 0x002D), OnOff, and ModeSelect.
  // Apple Home renders the device as an air purifier; the OnOff cluster
  // gives a simple toggle, and ModeSelect surfaces the 5-state hardware
  // cycle (Off/On/1H/3H/6H) in the accessory's settings page.
  // let mistEndpoint = Matter.AirPurifier(node: rootNode)
  // var mistIsOn: Bool = false
  // mistEndpoint.eventHandler = { event in
  //   switch event.attribute {
  //   case .onOff:
  //     let targetOn = event.value != 0
  //     print("[HUMI] 💧 AirPurifier OnOff target=\(targetOn) mistIsOn=\(mistIsOn)")
  //     if targetOn != mistIsOn {
  //       fanButton.press()
  //       mistIsOn = targetOn
  //     }
  //   case .currentMode:
  //     let targetMode = UInt8(event.value & 0xFF)
  //     print("[HUMI] 🪀 AirPurifier CurrentMode target=\(targetMode) hwState=\(hwState)")
  //     let presses = (Int(targetMode) - Int(hwState) + 5) % 5
  //     for i in 0..<presses {
  //       if i > 0 { delay_ms(200) }
  //       fanButton.press()
  //     }
  //     hwState = targetMode
  //     mistIsOn = (hwState != 0)
  //   case .fanMode, .percentSetting:
  //     // FanControl writes from Home; treat as a mode change request.
  //     print("[HUMI] 🌬️ AirPurifier FanControl attribute received value=\(event.value)")
  //   default:
  //     return
  //   }
  // }

  // (3) Add the endpoints to the node
  // rootNode.addEndpoint(fanEndpoint)    // TODO GUARD CUIDAO! MOSCA FIXME
  // rootNode.addEndpoint(modeSelectEndpoint)
  rootNode.addEndpoint(mistEndpoint)
  rootNode.addEndpoint(lightEndpoint)
  // rootNode.addEndpoint(switchEndpoint)

  // (4) Start Matter
  let app = Matter.Application()
  app.rootNode = rootNode
  app.start()

  // Main loop: poll panel sensors and sync state back to Matter.
  // Keep local variables alive — workaround for swift-matter-examples issue #10.
  var lastMistState: UInt8 = 255  // sentinel — forces a sync on first read
  var pendingMistState: UInt8 = 0
  var pendingMistCount: Int = 0
  let kMistDebounceCount = 2      // consecutive identical reads required (≈ 400 ms)
  while true {
    // MIST: state-based sensing from the panel-LED encoding. Each iteration
    // reads the current hardware mode (Off/On/1H/3H/6H) and only reports a
    // change once the same state has been observed kMistDebounceCount times
    // in a row. Filters out brief transients — notably the panel's empty-tank
    // signal, which momentarily lifts L4 to 4.5 V (the mode-1 inactive-col
    // pattern) before snapping the device back to off.
    let newRead = matter_read_mist_state()
    if newRead == pendingMistState {
      if pendingMistCount < kMistDebounceCount { pendingMistCount += 1 }
    } else {
      pendingMistState = newRead
      pendingMistCount = 1
    }
    if pendingMistCount >= kMistDebounceCount && pendingMistState != lastMistState {
      print("[HUMI] 💧 mist state change \(lastMistState) → \(pendingMistState)")
      hwState = pendingMistState
      let newOn = (hwState != 0)
      if newOn != mistIsOn {
        mistIsOn = newOn
        mistEndpoint.update(mistIsOn)
      }
      lastMistState = pendingMistState
    }

    // LIGHT: still edge-detected (no LED feedback on the panel for this button).
    if matter_lamp_button_was_pressed() {
      print("[HUMI] 💡 LIGHT button press detected")
      lampIsOn = !lampIsOn
      lightEndpoint.update(lampIsOn)
      // switchEndpoint.press()
      // lampButton.press()  // Simulate the button press on the hardware — disabled: see fanButton.press above
    }
    delay_ms(200)
  }
}
