@_cdecl("app_main")
func main() {
  print("[HUMI] Hello, Embedded Swift! (Humidifier / Fan device) 🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰🍰")

  let fanButton  = ButtonShunt(gpio: fanButtonGPIO)
  setup_mist_panel_sensors(mistRowGPIO, mistColAGPIO, mistColBGPIO)

  let lampButton = ButtonShunt(gpio: lampButtonGPIO)
  setup_lamp_listen_gpio(lampListenGPIO)

  // Assumed hardware states on boot.
  var hwState: UInt8 = 0   // fan: Off
  var lampIsOn: Bool = false
  var lampPhysicalPressActive = false
  var suppressLampDetectTicks: Int = 0

  // (1) Create a Matter root node
  let rootNode = Matter.Node()
  rootNode.identifyHandler = { print("[HUMI] I am a humidifier") }



  // (2.5) Create an OnOff Light endpoint for the lamp (S2 button)
  let lightEndpoint = Matter.OnOffLight(node: rootNode)
  lightEndpoint.eventHandler = { event in
    guard case .onOff = event.attribute else { return }
    let targetOn = event.value != 0
    if targetOn != lampIsOn {
      lampButton.press()
      lampIsOn = targetOn
      suppressLampDetectTicks = 4   // 4 × 200 ms = 800 ms suppression
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
  var lastLampPulseCount: UInt32 = matter_lamp_pulse_count()
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

    // LIGHT: pulse-absence detection on the S2 scan line (GPIO19).
    // The humidifier MCU pulses S2 at 100 Hz; a physical press suppresses them.
    let currentLampPulseCount = matter_lamp_pulse_count()
    // print("[HUMI] 🔦 lamp count=\(currentLampPulseCount) last=\(lastLampPulseCount) active=\(lampPhysicalPressActive)")
    if suppressLampDetectTicks > 0 {
      suppressLampDetectTicks -= 1
    } else {
      let pulsesAbsent = (currentLampPulseCount == lastLampPulseCount)
      if pulsesAbsent && !lampPhysicalPressActive {
        lampPhysicalPressActive = true
      } else if !pulsesAbsent && lampPhysicalPressActive {
        lampPhysicalPressActive = false
        print("[HUMI] 💡 S2 physical press detected")
        lampIsOn = !lampIsOn
        lightEndpoint.update(lampIsOn)
      }
    }
    lastLampPulseCount = currentLampPulseCount
    delay_ms(200)
  }
}
