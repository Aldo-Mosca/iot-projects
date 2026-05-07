# ESP-IDF v5.4.1 + esp-matter v1.4 Environment Setup
## Context

This document records the environment setup steps taken to prepare for building
a Matter humidifier accessory in Embedded Swift on a Seeed Studio XIAO ESP32-C6.
The goal is a clean ESP-IDF v5.4.1 + esp-matter release/v1.4 environment
coexisting alongside the existing ESP-IDF v5.2.1 + esp-matter release/v1.2 setup.

**Project root:** `$HOME/Local-Documents/repos/IoT-projects/`

```
IoT-projects/
├── esp/
│   ├── esp-matter/          # existing: esp-matter release/v1.2 (paired with IDF v5.2.1)
│   └── esp-matter-v1.4/     # new: esp-matter release/v1.4 (paired with IDF v5.4.1)
├── swift-matter-examples/   # upstream Apple repo (to be forked into Matter-Humidifier)
├── Matter-Humidifier/       # project target (Embedded Swift firmware)
└── docs/
    └── DEVLOG.md
```

**ESP-IDF installs:**
- `~/Local-Documents/repos/IoT-projects/esp/esp-idf`         — existing v5.2.1
- `~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1` — new, installed during this session

---

## What Was Already Working

- ESP-IDF v5.2.1 + esp-matter release/v1.2 installed and functional.
- C++ `examples/light` builds, flashes, and commissions into Apple Home
  (passcode `20202021`).
- Embedded Swift path partially working but blocked: `esp_matter.endpoint.get_device_type_ids`
  was removed from esp-matter at commit `9d7ff306`. This API is called by
  `swift-matter-examples`' C++ shim layer (`MatterInterface.cpp`).

---

## Why We Upgraded

| Component     | Old version       | New version         | Reason                                              |
|---------------|-------------------|---------------------|-----------------------------------------------------|
| ESP-IDF       | v5.2.1            | **v5.4.1**          | Required by esp-matter main/v1.4; v5.2.x unsupported |
| esp-matter    | release/v1.2      | **release/v1.4**    | Stable release; v1.2 API too old for Swift shim     |
| swift-matter-examples | pinned old commit | **main HEAD** | Will be adapted (see Step 2 onward)              |

esp-matter `release/v1.4` tracks Matter spec v1.4 and is the newest stable
branch. `main` is the ongoing v1.5 effort — we avoid it for stability.

---

## Tool Layout After Setup

All IDF versions share `~/.espressif` — this is safe because IDF namespaces
everything by version internally:

```
~/.espressif/
├── python_env/
│   ├── idf5.2_py3.14_env/   # venv for IDF v5.2.1
│   ├── idf5.4_py3.14_env/   # venv for IDF v5.4.1  ← target for this work
│   ├── idf5.5_py3.14_env/
│   └── idf6.0_py3.14_env/
└── tools/
    └── riscv32-esp-elf/
        ├── esp-13.2.0_20230928/
        ├── esp-14.2.0_20241119/
        └── esp-15.2.0_20251204/
```

No separate `IDF_TOOLS_PATH` override is needed — the default `~/.espressif`
works for all versions simultaneously.

---

## Steps Completed This Session

### Step 1a — Clone ESP-IDF v5.4.1

```bash
git clone -b v5.4.1 --depth 1 https://github.com/espressif/esp-idf.git \
  ~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1

cd ~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1
git submodule update --init --depth 1
export PATH="/opt/homebrew/bin:$PATH" # Ensure you get the correct python3 version. This is important inside the firewall

export IDF_PATH=~/Documents/repos/iot-projects/esp/esp-idf-v5.4.1
cd $IDF_PATH
./install.sh esp32c6
```

### Step 1b — Clone esp-matter release/v1.4

```bash
cd ~/Local-Documents/repos/IoT-projects/esp/

git clone --depth 1 -b release/v1.4 \
  https://github.com/espressif/esp-matter.git \
  esp-matter-v1.4

cd esp-matter-v1.4
git submodule update --init --depth 1
cd connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --shallow --platform esp32 darwin
cd ../..
```

### Step 1c — Run esp-matter install.sh

**Critical:** source IDF v5.4.1 *before* running `install.sh` so that the
IDF-managed Python venv (`idf5.4_py3.14_env`) is on PATH. If you skip this,
`pip` resolves to Homebrew Python and fails with a PEP 668 error
("externally-managed-environment").

```bash
# Fresh terminal
source ~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1/export.sh
ORRRRRR
( 
  cd esp-idf-v5.4.1
  IDF_TOOLS_PATH=~/.espressif-v5.4.1 ./install.sh esp32c6
)

# Confirm correct Python (must NOT be /usr/bin or /opt/homebrew)
which python3   # expected: ~/.espressif/python_env/idf5.4_py3.14_env/bin/python3

cd ~/Local-Documents/repos/IoT-projects/esp/esp-matter-v1.4
./install.sh --no-host-tool
```

**Why `--no-host-tool`:** The host tools build (chip-tool, chip-cert) compiles
connectedhomeip natively using Apple clang. Recent macOS clang versions reject
the `operator"" _span` syntax in `connectedhomeip/src/lib/support/Span.h` as a
hard error (`-Werror,-Wdeprecated-literal-operator`). We don't need chip-tool
anyway — commissioning is done via Apple Home directly.

**If pip still fails after sourcing IDF:** run `hash -r` to force the shell to
rescan PATH, then re-check `which python3`.

### Step 1d — Shell Aliases

Add to `~/.zshrc`:

```zsh
# ESP-IDF v5.2.1 + esp-matter v1.2 (existing, for C++ light example)
alias get_esp='source ~/Local-Documents/repos/IoT-projects/esp/esp-idf/export.sh && \
  source ~/Local-Documents/repos/IoT-projects/esp/esp-matter/export.sh'

# ESP-IDF v5.4.1 + esp-matter v1.4 (for Embedded Swift / Matter-Humidifier)
alias get_esp541='source ~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1/export.sh && \
  source ~/Local-Documents/repos/IoT-projects/esp/esp-matter-v1.4/export.sh'
```

### Step 1e — Verify

```bash
# Fresh terminal
get_esp541
idf.py --version        # expected: ESP-IDF v5.4.1
echo $ESP_MATTER_PATH   # expected: .../esp-matter-v1.4
which python3           # expected: inside ~/.espressif/python_env/idf5.4_py3.14_env
```

---

## Known Issues / Pitfalls

| Issue | Cause | Fix |
|-------|-------|-----|
| PEP 668 pip error during `install.sh` | Homebrew Python on PATH; IDF venv not active | Source `~/Local-Documents/repos/IoT-projects/esp/esp-idf-v5.4.1/export.sh` before running `install.sh` |
| `operator"" _span` clang error in `install.sh` | Apple clang rejects deprecated UDL syntax in connectedhomeip Span.h | Use `./install.sh --no-host-tool` |
| `idf5.5_py3.14_env` or `idf6.0_py3.14_env` activating instead of `idf5.4` | Wrong IDF sourced, or stale PATH from previous session | Always use a fresh terminal; source only one IDF version per session |
| `get_device_type_ids` build error in swift-matter-examples | API removed from esp-matter at commit `9d7ff306` | Addressed in Step 3 (MatterInterface.cpp surgery) — not yet done |

---

## Next Steps (Not Yet Done)

**Step 2 — Scaffold Matter-Humidifier project**
Copy `swift-matter-examples/smart-light` into `Matter-Humidifier/` as the
starting point. Do not modify the upstream `swift-matter-examples` repo.

**Step 3 — Fix MatterInterface.cpp / MatterInterface.h**
Remove the `get_device_type_ids` call from the C++ shim. Replace with a
statically-passed device type ID. The target device type is
**On/Off Plug-in Unit** (Matter device type ID `0x010A`) — the standard type
for a switched appliance, shows up in Apple Home as a switchable outlet.

**Step 4 — Fix Matter.swift / Node.swift**
Update the Swift abstraction layer to pass the device type ID constant at init
rather than querying it dynamically via the shim.

**Step 5 — Write Matter.Humidifier**
New Swift class replacing `Matter.ExtendedColorLight`. Wires the on/off cluster
attribute callback to a GPIO pin driving the 5V relay module →
ultrasonic transducer VRK signal.

**Step 6 — app_main.cpp**
Adapted entry point: humidifier device type, no LED driver dependency.

---

## Hardware Reference (XIAO ESP32-C6)

- **Relay control GPIO:** TBD — to be confirmed during Step 5
- **Relay module:** 5V active-high module from parts collection
- **Final power:** MP1584 or LM2596 buck converter (not yet installed;
  bench work uses MB102 breadboard PSU)
- **Commissioning passcode:** `20202021` (test default)
- **Monitor exit:** `Ctrl+]`
- **USB enumeration reset:** hold BOOT, tap RESET, release BOOT

## Build Procedure (once environment is set up)

```bash
# Fresh terminal every time
get_esp541
cd ~/Local-Documents/repos/IoT-projects/Matter-Humidifier
idf.py set-target esp32c6
TOOLCHAINS=<nightly-swift-toolchain-id> idf.py build flash monitor
```

The `TOOLCHAINS` env var must be set to the nightly trunk Swift toolchain
bundle identifier (e.g. `org.swift.59202407151a`) — NOT Xcode Swift.
Check the current value with:
```bash
plutil -extract CFBundleIdentifier raw \
  /Library/Developer/Toolchains/swift-latest.xctoolchain/Info.plist
```
