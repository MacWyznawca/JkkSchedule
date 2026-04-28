/*
 * JkkSchedule — "Arduino as ESP-IDF component" example
 *
 * Status: NOT TESTED
 *
 * In this variant arduino-esp32 is an ESP-IDF component, NOT the top-level
 * build system. This means:
 *   - full ESP-IDF APIs (NVS, FreeRTOS, timers, …) are available natively
 *   - Arduino helpers (Serial, millis(), WiFi, …) are available via the
 *     arduino component
 *   - JkkSchedule FreeRTOS timers work exactly as in a pure ESP-IDF project
 *   - this is the recommended Arduino-compatible path for this library
 *
 * Build:
 *   idf.py set-target esp32s3
 *   idf.py build
 *   idf.py flash monitor
 */

#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <esp_sntp.h>
#include "JkkSchedule.h"

/* ------------------------------------------------------------------ */
/* Configuration — edit before building                                */
/* ------------------------------------------------------------------ */
static const char *WIFI_SSID     = "YourSSID";
static const char *WIFI_PASSWORD = "YourPassword";

/* Geo coordinates in E5 format (degrees × 100 000).
 * Examples:
 *   Warsaw:  lat=5222970,  lon=2101220
 *   London:  lat=5150790,  lon=  -12578
 *   New York: lat=4071280, lon=-7400060
 */
static const int32_t MY_LATITUDE  = 5222970;   /* 52.2297° N */
static const int32_t MY_LONGITUDE = 2101220;   /* 21.0122° E */

/* Timezone string (POSIX TZ).  Replace as needed.
 * https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv */
static const char *TZ_STRING = "CET-1CEST,M3.5.0/2,M10.5.0/3";
/* ------------------------------------------------------------------ */

static jkk_schedules_handle_t *schedules = nullptr;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void schedule_trigger_cb(jkk_schedule_handle_t handle, void *priv_data)
{
    (void)handle;
    const char *name = static_cast<const char *>(priv_data);
    Serial.printf("[JkkSchedule] Triggered: %s\n", name ? name : "unnamed");
}

static bool wait_for_wifi(uint32_t timeout_ms)
{
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        delay(250);
    }
    return false;
}

static bool wait_for_time_sync(uint32_t timeout_ms)
{
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        time_t now = time(nullptr);
        if (now >= JKK_ERA) {
            return true;
        }
        delay(250);
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* setup / loop — Arduino entry points                                 */
/* ------------------------------------------------------------------ */

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("JkkSchedule — arduino-as-idf-component example");

    /* NVS init (same as pure ESP-IDF) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* WiFi (needed for SNTP; sunrise/sunset also needs correct time) */
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    if (!wait_for_wifi(15000)) {
        Serial.println("\nWiFi failed — continuing without time sync.");
    } else {
        Serial.println("\nWiFi connected.");

        /* SNTP — configure before initialising JkkSchedule */
        setenv("TZ", TZ_STRING, 1);
        tzset();
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.nist.gov");
        esp_sntp_init();

        Serial.print("Waiting for time sync");
        if (!wait_for_time_sync(20000)) {
            Serial.println("\nTime sync timed out.");
        } else {
            Serial.println("\nTime synced.");
        }
    }

    /* Initialise JkkSchedule
     * - NVS partition: NULL  → default "nvs"
     * - NVS namespace: "sched_demo"
     * - Latitude  E5: MY_LATITUDE
     * - Longitude E5: MY_LONGITUDE
     */
    schedules = jkk_schedule_init(NULL, (char *)"sched_demo",
                                  MY_LATITUDE, MY_LONGITUDE);
    if (!schedules) {
        Serial.println("Failed to initialise JkkSchedule!");
        return;
    }

    /* Load any schedules already stored in NVS */
    jkk_schedule_get_all(schedules);

    /* -------------------------------------------------------------- */
    /* Example: trigger every weekday at 08:30                        */
    /* -------------------------------------------------------------- */
    jkk_schedule_config_t cfg_morning = {};
    strlcpy(cfg_morning.name, "morning", sizeof(cfg_morning.name));
    cfg_morning.enabled              = true;
    cfg_morning.trigger.type         = JKK_SCHEDULE_TYPE_DAYS_OF_WEEK;
    cfg_morning.trigger.hours        = 8;
    cfg_morning.trigger.minutes      = 30;
    cfg_morning.trigger.days_of_week =
        JKK_DAY_MON | JKK_DAY_TUE | JKK_DAY_WED | JKK_DAY_THU | JKK_DAY_FRI;
    cfg_morning.callback   = schedule_trigger_cb;
    cfg_morning.priv_data  = (void *)"morning";

    if (jkk_schedule_add(schedules, &cfg_morning) != ESP_OK) {
        Serial.println("Failed to add 'morning' schedule.");
    }

    /* -------------------------------------------------------------- */
    /* Example: trigger at sunrise every day                           */
    /* -------------------------------------------------------------- */
    jkk_schedule_config_t cfg_sunrise = {};
    strlcpy(cfg_sunrise.name, "sunrise", sizeof(cfg_sunrise.name));
    cfg_sunrise.enabled          = true;
    cfg_sunrise.trigger.type     = JKK_SCHEDULE_TYPE_DATE_SUNRISE;
    cfg_sunrise.trigger.offset   = 0;          /* 0 = exactly at sunrise */
    cfg_sunrise.callback         = schedule_trigger_cb;
    cfg_sunrise.priv_data        = (void *)"sunrise";

    if (jkk_schedule_add(schedules, &cfg_sunrise) != ESP_OK) {
        Serial.println("Failed to add 'sunrise' schedule.");
    }

    Serial.println("JkkSchedule initialised, schedules armed.");

    /* Preview next sunrise time */
    time_t now = time(nullptr);
    if (now >= JKK_ERA) {
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        int sr_h, sr_m;
        JkkSuntimeNow(schedules, JKK_SUN_RISE, &sr_h, &sr_m);
        Serial.printf("Next sunrise: %02d:%02d (local time)\n", sr_h, sr_m);
    }
}

void loop()
{
    /* JkkSchedule runs on its own FreeRTOS timer — nothing needed here.
     * Add your application logic as needed. */
    delay(10000);
    Serial.println("loop — still running");
}
