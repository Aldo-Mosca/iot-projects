@_cdecl("app_main")
func main() {
  print("[HUMI] Hello, Embedded Swift! (Humidifier / Fan device) 🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰")

  let fanButton  = ButtonShunt(gpio: fanButtonGPIO)
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
  // (3) Add the endpoints to the node
  rootNode.addEndpoint(mistEndpoint)
  rootNode.addEndpoint(lightEndpoint)

  // (4) Start Matter
  let app = Matter.Application()
  app.rootNode = rootNode
  app.start()

  // Main loop: poll panel sensors and sync state back to Matter.
  // Keep local variables alive — workaround for swift-matter-examples issue #10.
  var lastMistState: UInt8 = 255  // sentinel — forces a sync on first read
  var pendingMistState: UInt8 = 0
  var pendingMistCount: Int = 0
  let kMistDebounceCount = 2      // consecutive identical reads required (≈ 400 ms at 200 ms tick)
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
    }
    delay_ms(200)
  }
}
