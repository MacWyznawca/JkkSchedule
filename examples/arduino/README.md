# Arduino Examples

This folder contains experimental Arduino-related examples for ESP32.

**Status: NOT TESTED** — provided as starting points only.

## Examples

### `basic_not_tested/`

Plain Arduino IDE / PlatformIO (Arduino framework) usage.
Adds JkkSchedule as a library manually.
This path may require extra work because NVS and FreeRTOS timer APIs must
come from the Arduino core and may not be fully exposed.

### `arduino_as_idf_component_not_tested/`

**Recommended Arduino-compatible path.**
Builds with `idf.py` (ESP-IDF build system); arduino-esp32 is added as a
regular ESP-IDF component. Full ESP-IDF APIs (NVS, FreeRTOS timers) are
available natively, so JkkSchedule is expected to work without extra glue.
See the subfolder `README.md` for step-by-step instructions.

---

Important:
- JkkSchedule is primarily an ESP-IDF library
- Arduino support is experimental in both variants
