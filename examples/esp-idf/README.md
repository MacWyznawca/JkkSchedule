# ESP-IDF Examples

Tested with ESP-IDF 5.5, 5.6 and 6.0.

## Build Basic Example

```bash
cd examples/esp-idf/basic
idf.py set-target esp32
idf.py build
```

## Build Sun Events Example

```bash
cd examples/esp-idf/sun_events
idf.py set-target esp32
idf.py build
```

## Notes

- Examples use `EXTRA_COMPONENT_DIRS` to consume JkkSchedule from repository root.
- Make sure NVS is initialized before using scheduler APIs.
- For sunrise/sunset accuracy, device time and timezone must be configured.
