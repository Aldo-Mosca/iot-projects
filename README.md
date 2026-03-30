# IoT Projects

Personal repository for IoT hardware and firmware projects, focused on the **Matter** smart home protocol and the **Seeed Studio XIAO ESP32-C6** platform.

## Structure

```
iot-projects/
├── docs/               ← Project documentation and development logs
└── Matter-Humidifier/  ← Matter-enabled humidifier firmware (in progress)
```

## Projects

### Matter Humidifier

Adding Matter smart home capabilities to a room humidifier using a Seeed XIAO ESP32-C6. The ESP32-C6's native Thread (802.15.4) support makes it a natural fit for Matter-over-Thread with Apple Home.

See [`docs/matter-humidifier-devlog.md`](docs/matter-humidifier-devlog.md) for the full development log.

### Swift Matter Smart Light

Getting Apple's [`swift-matter-examples/smart-light`](https://github.com/swiftlang/swift-matter-examples) project building and running on the Seeed XIAO ESP32-C6 with Embedded Swift is the "Hello, World!" project of ESP32 + Matter. Covers all the dependency and build system fixes needed to make the 2024 example work with older swift abd esp-idf toolchains.  

See [`docs/swift-matter-smart-light-xiao-esp32c6.md`](docs/swift-matter-smart-light-xiao-esp32c6.md) for the full fix log.

## Stack

- **MCU**: Seeed Studio XIAO ESP32-C6 (ESP32-C6FH4, RISC-V, Thread + Wi-Fi 6 + BLE)
- **Framework**: ESP-IDF v5.2.1 + esp-matter release/v1.2
- **Language**: Embedded Swift (application layer) over ESP Matter C++ SDK
- **Ecosystem**: Apple Home (Matter-over-Thread / Matter-over-WiFi)
- **Host**: macOS (Apple Silicon)

### TODOs
- [ ] Adapt code to work with the [recently released Swift toolchain](https://www.swift.org)
- [ ] Implement the Humidifier-Matter bridge