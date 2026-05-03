# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.6] - 2026-05-03

### Fixed

- **Sunrise/sunset schedule could fire twice with a fixed-time boundary** — When a
  sunrise or sunset schedule had both an astronomical trigger and a fixed boundary
  time, a post-trigger re-evaluation could incorrectly pick the boundary again later
  the same day. This happened because the next sun event was already on the next day,
  while the fixed boundary diff still pointed to "today". The scheduler now skips the
  same-day boundary once the matching sun event has already passed and correctly waits
  for the next astronomical event.

## [1.0.5] - 2026-05-02

### Added

- **Simplified Chinese README** (`README_CN.md`) — ESP Component Registry and GitHub
  serve the appropriate language based on the browser's `Accept-Language` header.

## [1.0.4] - 2026-05-02

### Fixed

- **Sunrise/sunset with negative offset fires at wrong time** — When a schedule used
  `JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE` with a negative `relative_seconds` offset
  (e.g. `-900` = 15 minutes *before* sunrise), the "advance to tomorrow" guard compared
  the current time against the **raw sunrise minute** instead of the **effective trigger
  time** (sunrise + offset). If the scheduler was evaluated in the window between the
  effective trigger and actual sunrise, the sun-based diff became negative, which as a
  `uint32_t` wrapped to a huge value. The min/max selection in
  `jkk_schedule_get_next_schedule_time_diff` then picked the fallback fixed-time (cap)
  instead of the sun trigger. The fix uses `_cur_sec >= (int)sun * 60 + relative_seconds`
  as the guard condition. The analogous positive-offset case for SUNSET was also
  affected and is corrected by the same change.

## [1.0.3] - 2025

### Changed

- Cosmetic code changes.

## [1.0.2] - 2025

### Added

- Description of sunrise/sunset time boundary behaviour.
- Polish README (`README_PL.md`).

## [1.0.1] - 2025

### Added

- Initial ESP Component Registry release setup.
