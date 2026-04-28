/* JkkSchedule - Lightweight scheduling library for ESP-IDF
 * Copyright (C) 2025 Jaromir Kopp (JKK)
 * Internal declarations used by scheduler core and persistence layer
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "JkkSchedule.h"

esp_err_t jkk_schedule_nvs_add(jkk_schedules_handle_t *schedules_h, jkk_schedule_t *schedule);
esp_err_t jkk_schedule_nvs_remove(jkk_schedules_handle_t *schedules_h, jkk_schedule_t *schedule);
int8_t jkk_schedule_nvs_get_all(jkk_schedules_handle_t *schedules_h);
bool jkk_schedule_nvs_is_enabled(jkk_schedules_handle_t *schedules_h);