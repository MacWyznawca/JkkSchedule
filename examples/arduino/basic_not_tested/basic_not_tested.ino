/*
 * JkkSchedule Arduino example
 *
 * Status: NOT TESTED
 *
 * This sketch is provided as a starting point for Arduino-ESP32 users.
 * It was not compiled or run in an Arduino environment yet.
 */

extern "C" {
#include "JkkSchedule.h"
#include "nvs_flash.h"
}

#include <Arduino.h>
#include <time.h>

static const char *TAG_NAME = "arduino_demo";

static void schedule_trigger_cb(jkk_schedule_handle_t handle, void *priv_data)
{
    (void)handle;
    const char *name = static_cast<const char *>(priv_data);
    Serial.print("Triggered: ");
    Serial.println(name ? name : "unnamed");
}

static bool wait_for_time_sync(uint32_t timeout_ms)
{
    uint32_t started = millis();
    time_t now = 0;

    while ((millis() - started) < timeout_ms) {
        time(&now);
        if (now >= JKK_ERA) {
            return true;
        }
        delay(250);
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    /*
     * Configure time for sunrise/sunset support.
     * Replace timezone and SNTP servers as needed.
     */
    configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

    if (!wait_for_time_sync(20000)) {
        Serial.println("Time sync failed, scheduler may not start correctly.");
        return;
    }

    jkk_schedules_handle_t *schedules = jkk_schedule_init(NULL, (char *)"arduino", 5222970, 2101220);
    if (!schedules) {
        Serial.println("Failed to initialize JkkSchedule.");
        return;
    }

    (void)jkk_schedule_get_all(schedules);

    jkk_schedule_config_t config = {0};
    strlcpy(config.name, TAG_NAME, sizeof(config.name));
    config.enabled = true;
    config.trigger.type = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK;
    config.trigger.hours = 9;
    config.trigger.minutes = 0;
    config.trigger.day.repeat_days = JKK_SCHEDULE_DAY_EVERYDAY;
    config.trigger_cb = schedule_trigger_cb;
    config.priv_data = (void *)TAG_NAME;

    jkk_schedule_handle_t handle = jkk_schedule_find(schedules, config.name);
    if (!handle) {
        handle = jkk_schedule_create(schedules, &config);
        if (handle) {
            ESP_ERROR_CHECK(jkk_schedule_add(schedules, handle));
        }
    } else {
        ESP_ERROR_CHECK(jkk_schedule_edit(schedules, handle, &config));
    }

    ESP_ERROR_CHECK(jkk_schedule_run_all(schedules, false));
    Serial.println("JkkSchedule started.");
}

void loop()
{
    delay(1000);
}
