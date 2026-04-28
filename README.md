# JkkSchedule

JkkSchedule is a lightweight schedule library for ESP32 and ESP-IDF projects.

It supports:
- day-of-week schedules
- date schedules
- relative interval schedules
- sunrise/sunset based schedules with per-device geo position
- persistent storage in NVS
- single shared master timer for efficient runtime behavior

## Project Status

This library is production-oriented and extracted as a standalone component from a larger private project.

### Inspiration and Rewrite Note

JkkSchedule was inspired by the original Espressif schedule component.
This implementation is not a drop-in copy.
It has been heavily refactored and corrected, including calendar handling fixes, sunrise/sunset behavior fixes, and a redesigned timer orchestration strategy.

## License

Apache License 2.0. See [LICENSE](LICENSE).

## Requirements

- ESP-IDF 5.5, 5.6, 6.0 (tested target range)
- FreeRTOS (provided by ESP-IDF)
- NVS (`nvs_flash` component)

## Installation (ESP-IDF Component)

Copy this repository as a component:
- `components/JkkSchedule`

Or add it as a git submodule inside your project components directory.

In your project source:

```c
#include "JkkSchedule.h"
```

## Quick Start

```c
jkk_schedules_handle_t *sched = jkk_schedule_init(
    NULL,
    "sched0",
    5212345,   // latitude * 100000
    2101234    // longitude * 100000
);

if (sched) {
    jkk_schedule_get_all(sched);
    jkk_schedule_run_all(sched, false);
}
```

## Basic Usage Pattern

1. Create manager with `jkk_schedule_init(...)`
2. Load NVS with `jkk_schedule_get_all(...)`
3. Register callbacks (`jkk_schedule_callback_all(...)` or per schedule)
4. Create or edit schedules
5. Start runtime with `jkk_schedule_run_all(...)`

## API Notes

- Coordinates are passed as `int32_t` in E5 format.
- Sunrise/sunset modes use coordinates from manager handle.
- If coordinates are invalid, sunrise/sunset scheduling falls back to fixed hour/minute behavior.

## Examples

See:
- [examples/esp-idf/basic](examples/esp-idf/basic)
- [examples/esp-idf/sun_events](examples/esp-idf/sun_events)

## Arduino Compatibility

The library is designed for ESP-IDF components.

It can be used from Arduino only under these conditions:
- target platform is ESP32 (Arduino-ESP32)
- project is built with ESP-IDF integration (for example Arduino as ESP-IDF component)
- `nvs_flash` is initialized before schedule loading
- standard C time functions are available and SNTP/timezone are configured for sunrise/sunset accuracy

For plain Arduino Library Manager style usage (without ESP-IDF component model), adaptation is required because this code depends on:
- `esp_err.h`
- FreeRTOS timers
- ESP-IDF NVS API

## Repository Layout

- `include/JkkSchedule.h` - public API
- `src/JkkSchedule.c` - scheduler core
- `src/JkkScheduleNvs.c` - NVS persistence layer
- `src/JkkScheduleInternal.h` - internal declarations
- `examples/` - reference integration projects

## Acknowledgements

- Espressif Systems for publishing the original schedule concept.
- Contributors and users validating behavior on real devices.
