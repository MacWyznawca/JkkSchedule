# Arduino as ESP-IDF Component — JkkSchedule example

**Status: NOT TESTED** — provided as a starting point only.

## What is "Arduino as ESP-IDF component"?

Instead of building with the Arduino IDE or PlatformIO in Arduino mode,
you build with `idf.py` (the standard ESP-IDF build system) and add
[arduino-esp32](https://github.com/espressif/arduino-esp32) as a regular
ESP-IDF component. This gives you:

- full access to all ESP-IDF APIs (NVS, FreeRTOS timers, …)
- Arduino helpers such as `Serial`, `millis()`, `WiFi`, …
- predictable component-level dependency resolution

JkkSchedule uses NVS and FreeRTOS timers internally, so this variant is
**much more likely to work out of the box** than plain Arduino IDE usage.

## Project layout

```
arduino_as_idf_component_not_tested/
├── CMakeLists.txt          ← top-level ESP-IDF project file
├── sdkconfig.defaults      ← minimal arduino-esp32 Kconfig overrides
├── main/
│   ├── CMakeLists.txt      ← component registration
│   ├── idf_component.yml   ← optional: fetch JkkSchedule via IDF component manager
│   └── main.cpp            ← application (setup / loop)
└── components/             ← created manually, see below
    └── arduino/            ← cloned arduino-esp32
```

## Getting started

### 1. Prerequisites

- ESP-IDF v5.x or v6.x installed and activated (`export.sh`)
- Internet access for SNTP time sync at runtime

### 2. Clone arduino-esp32

```bash
cd arduino_as_idf_component_not_tested
mkdir components
git clone https://github.com/espressif/arduino-esp32 components/arduino
```

### 3. Add JkkSchedule (choose one option)

**Option A — component manager (recommended)**

The `main/idf_component.yml` already declares the dependency.
The build system fetches it automatically on first `idf.py build`.

**Option B — manual clone**

```bash
git clone https://github.com/MacWyznawca/JkkSchedule components/JkkSchedule
```

Remove or rename `main/idf_component.yml` if you use this option,
then update `REQUIRES` in `main/CMakeLists.txt` from `JkkSchedule`
to whatever component name you used.

### 4. Edit `main/main.cpp`

Set your WiFi credentials, geo coordinates, and timezone string at the top
of the file.

### 5. Build, flash, monitor

```bash
idf.py set-target esp32s3   # or esp32, esp32s2, esp32c3, …
idf.py build
idf.py flash monitor
```
