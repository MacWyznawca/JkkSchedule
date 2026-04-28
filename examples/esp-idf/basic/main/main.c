#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "JkkSchedule.h"

static const char *TAG = "JkkScheduleBasic";

static void example_schedule_cb(jkk_schedule_handle_t handle, void *priv_data)
{
    (void)handle;
    const char *name = (const char *)priv_data;
    ESP_LOGI(TAG, "Triggered schedule: %s", name ? name : "unnamed");
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Example coordinates: Warsaw, Poland in E5 format */
    jkk_schedules_handle_t *schedules = jkk_schedule_init(NULL, "demo", 5222970, 2101220);
    if (!schedules) {
        ESP_LOGE(TAG, "Failed to initialize JkkSchedule");
        return;
    }

    (void)jkk_schedule_get_all(schedules);

    jkk_schedule_config_t cfg = {
        .name = "daily_09_00",
        .enabled = true,
        .trigger = {
            .type = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK,
            .hours = 9,
            .minutes = 0,
            .day.repeat_days = JKK_SCHEDULE_DAY_EVERYDAY,
        },
        .trigger_cb = example_schedule_cb,
        .priv_data = (void *)"daily_09_00",
    };

    jkk_schedule_handle_t h = jkk_schedule_find(schedules, cfg.name);
    if (!h) {
        h = jkk_schedule_create(schedules, &cfg);
        if (h) {
            ESP_ERROR_CHECK(jkk_schedule_add(schedules, h));
        }
    } else {
        ESP_ERROR_CHECK(jkk_schedule_edit(schedules, h, &cfg));
    }

    ESP_ERROR_CHECK(jkk_schedule_run_all(schedules, false));
    ESP_LOGI(TAG, "Scheduler started");
}
