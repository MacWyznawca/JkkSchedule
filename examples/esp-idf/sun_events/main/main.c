#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "JkkSchedule.h"

static const char *TAG = "JkkScheduleSun";

static void sunrise_cb(jkk_schedule_handle_t handle, void *priv_data)
{
    (void)handle;
    (void)priv_data;
    ESP_LOGI(TAG, "Sunrise schedule triggered");
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Example coordinates: Wroclaw, Poland in E5 format */
    jkk_schedules_handle_t *schedules = jkk_schedule_init(NULL, "sun", 5110720, 1703810);
    if (!schedules) {
        ESP_LOGE(TAG, "Failed to initialize JkkSchedule");
        return;
    }

    jkk_schedule_config_t sun_cfg = {
        .name = "sunrise_plus_15m",
        .enabled = true,
        .trigger = {
            .type = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE,
            .hours = 23,
            .minutes = 59,
            .relative_seconds = 15 * 60,
            .day.repeat_days = JKK_SCHEDULE_DAY_EVERYDAY,
        },
        .trigger_cb = sunrise_cb,
    };

    jkk_schedule_handle_t h = jkk_schedule_find(schedules, sun_cfg.name);
    if (!h) {
        h = jkk_schedule_create(schedules, &sun_cfg);
        if (h) {
            ESP_ERROR_CHECK(jkk_schedule_add(schedules, h));
        }
    } else {
        ESP_ERROR_CHECK(jkk_schedule_edit(schedules, h, &sun_cfg));
    }

    ESP_ERROR_CHECK(jkk_schedule_run_all(schedules, false));
    ESP_LOGI(TAG, "Sun events scheduler started");
}
