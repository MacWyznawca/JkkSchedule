# JkkSchedule

JkkSchedule is a lightweight schedule library for ESP32 and ESP-IDF projects.

It is packaged as a reusable ESP-IDF component and can be published through the ESP Component Registry.

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

- ESP-IDF 5.5, 6.0 (tested target range)
- FreeRTOS (provided by ESP-IDF)
- NVS (`nvs_flash` component)

## Installation (ESP-IDF Component)

From the ESP Component Registry:

```yaml
dependencies:
    MacWyznawca/JkkSchedule: "^1.0.0"
```

Or with the CLI:

```bash
idf.py add-dependency "MacWyznawca/JkkSchedule^1.0.0"
```

Manual options:

Copy this repository as a component:
- `components/JkkSchedule`

Or add it as a git submodule inside your project components directory.

In your project source:

```c
#include "JkkSchedule.h"
```

## Publishing Releases

Manual publish from a local ESP-IDF shell:

```bash
esp6
cd /Users/jkk/Documents/JkkSchedule
compote component upload --name JkkSchedule
```

Recommended release flow:

1. Update `version` in `idf_component.yml`
2. Commit the release
3. Create a matching git tag, for example `v1.0.1`
4. Push branch and tag to GitHub

This repository also includes a GitHub Actions workflow for automatic upload on pushed `v*` tags.
To enable it:

- set repository variable `ESP_COMPONENT_NAMESPACE`
- optionally set secret `IDF_COMPONENT_API_TOKEN`
- or configure OIDC trusted uploader in the ESP Component Registry for this repository and workflow file

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

## Sunrise/Sunset with Time Boundary

The `SUNRISE` and `SUNSET` schedule types support an optional **time boundary**. It is activated by setting valid `hours` (0–23) and `minutes` (0–59) fields in the trigger configuration. When both fields are out of range (default), the schedule fires purely at the astronomical time.

**Selection rule:**

| Type | Behaviour with boundary | Effect |
|---|---|---|
| `SUNRISE` | `min(sunrise, hours:minutes)` | No later than the given time |
| `SUNSET` | `max(sunset, hours:minutes)` | No earlier than the given time |

**Example — winter evenings:**
To prevent home lighting from switching to warm-white mode too early in winter (when sunset can be as early as 15:30), set type `SUNSET` with boundary `18:00`. The schedule fires at 18:00 in winter and at sunset in summer (when sunset is after 18:00):

```c
jkk_schedule_config_t cfg = {
    .name = "evening_warm_white",
    .trigger = {
        .type    = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET,
        .hours   = 18,   // no earlier than 18:00
        .minutes = 0,
        .day.repeat_days = JKK_SCHEDULE_DAY_EVERYDAY,
    },
    .trigger_cb = evening_cb,
};
```

**Analogously for SUNRISE** — `hours:minutes` acts as an upper limit (e.g. "wake-up no later than 07:30, even if sunrise is later in winter").

> If the geographic coordinates are not set or are invalid, the library uses only the fixed time — `hours:minutes` then behaves as a standard time-of-day trigger.

## Examples

See:
- [examples/esp-idf/basic](examples/esp-idf/basic)
- [examples/esp-idf/sun_events](examples/esp-idf/sun_events)
- [examples/arduino/basic_not_tested](examples/arduino/basic_not_tested)

## Arduino Compatibility

The library is designed for ESP-IDF components.

An experimental Arduino example is included in:
- [examples/arduino/basic_not_tested](examples/arduino/basic_not_tested)

Status:
- not tested
- provided as a starting point only

It can be used from Arduino only under these conditions:
- target platform is ESP32 (Arduino-ESP32)
- project is built with ESP-IDF integration (for example Arduino as ESP-IDF component)
- `nvs_flash` is initialized before schedule loading
- standard C time functions are available and SNTP/timezone are configured for sunrise/sunset accuracy

For plain Arduino Library Manager style usage (without ESP-IDF component model), adaptation is required because this code depends on:
- `esp_err.h`
- FreeRTOS timers
- ESP-IDF NVS API

The repository now also provides:
- `library.properties`
- `src/JkkSchedule.h` forwarding header for Arduino-style include discovery

## Repository Layout

- `include/JkkSchedule.h` - public API
- `src/JkkSchedule.c` - scheduler core
- `src/JkkScheduleNvs.c` - NVS persistence layer
- `src/JkkScheduleInternal.h` - internal declarations
- `examples/` - reference integration projects

## Acknowledgements

- Espressif Systems for publishing the original schedule concept.
- Contributors and users validating behavior on real devices.
