/* JkkSchedule - Lightweight scheduling library for ESP-IDF
 * Copyright (C) 2025 Jaromir Kopp (JKK)
 * Public API for schedule management, sunrise/sunset calculations and runtime control
 *
 * Inspired by the original Espressif esp_schedule component and significantly
 * redesigned with multiple fixes and behavior improvements.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

/** Opaque schedule handle. */
typedef void *jkk_schedule_handle_t;

#define EPOCH_TIMESTAMP (978307200l)
/** Seconds value used to verify that wall clock is already initialized. */
#define JKK_ERA (693792000l + EPOCH_TIMESTAMP)

/** Maximum number of schedules in a single manager handle. */
#define MAX_SCHEDULE_NUMBER 16

/** Maximum schedule name length. This also limits NVS key length. */
#define MAX_SCHEDULE_NAME_LEN 16

/** Callback executed when a schedule is triggered. */
typedef void (*jkk_schedule_trigger_cb_t)(jkk_schedule_handle_t handle, void *priv_data);

/** Callback executed whenever the next trigger timestamp is recalculated. */
typedef void (*jkk_schedule_timestamp_cb_t)(jkk_schedule_handle_t handle, uint32_t next_timestamp, void *priv_data);

/** Trigger type. */
typedef enum jkk_schedule_type {
    JKK_SCHEDULE_TYPE_INVALID = 0,
    JKK_SCHEDULE_TYPE_DAYS_OF_WEEK,
    JKK_SCHEDULE_TYPE_DATE,
    JKK_SCHEDULE_TYPE_RELATIVE,
    JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE,
    JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET,
    JKK_SCHEDULE_TYPE_DATE_SUNRISE,
    JKK_SCHEDULE_TYPE_DATE_SUNSET,
} jkk_schedule_type_t;

/** Bitmask of weekdays for JKK_SCHEDULE_TYPE_DAYS_OF_WEEK variants. */
typedef enum jkk_schedule_days {
    JKK_SCHEDULE_DAY_ONCE      = 0,
    JKK_SCHEDULE_DAY_EVERYDAY  = 0b1111111,
    JKK_SCHEDULE_DAY_MONDAY    = 1 << 0,
    JKK_SCHEDULE_DAY_TUESDAY   = 1 << 1,
    JKK_SCHEDULE_DAY_WEDNESDAY = 1 << 2,
    JKK_SCHEDULE_DAY_THURSDAY  = 1 << 3,
    JKK_SCHEDULE_DAY_FRIDAY    = 1 << 4,
    JKK_SCHEDULE_DAY_SATURDAY  = 1 << 5,
    JKK_SCHEDULE_DAY_SUNDAY    = 1 << 6,
} jkk_schedule_days_t;

/** Bitmask of months for JKK_SCHEDULE_TYPE_DATE variants. */
typedef enum jkk_schedule_months {
    JKK_SCHEDULE_MONTH_ONCE         = 0,
    JKK_SCHEDULE_MONTH_ALL          = 0b111111111111,
    JKK_SCHEDULE_MONTH_JANUARY      = 1 << 0,
    JKK_SCHEDULE_MONTH_FEBRUARY     = 1 << 1,
    JKK_SCHEDULE_MONTH_MARCH        = 1 << 2,
    JKK_SCHEDULE_MONTH_APRIL        = 1 << 3,
    JKK_SCHEDULE_MONTH_MAY          = 1 << 4,
    JKK_SCHEDULE_MONTH_JUNE         = 1 << 5,
    JKK_SCHEDULE_MONTH_JULY         = 1 << 6,
    JKK_SCHEDULE_MONTH_AUGUST       = 1 << 7,
    JKK_SCHEDULE_MONTH_SEPTEMBER    = 1 << 8,
    JKK_SCHEDULE_MONTH_OCTOBER      = 1 << 9,
    JKK_SCHEDULE_MONTH_NOVEMBER     = 1 << 10,
    JKK_SCHEDULE_MONTH_DECEMBER     = 1 << 11,
} jkk_schedule_months_t;

/** Trigger configuration. */
typedef struct jkk_schedule_trigger {
    bool enabled;
    /** Type of schedule */
    jkk_schedule_type_t type;
    /** Hours in 24 hour format. Accepted values: 0-23 */
    uint8_t hours;
    /** Minutes in the given hour. Accepted values: 0-59. */
    uint8_t minutes;
    /** For type JKK_SCHEDULE_TYPE_DAYS_OF_WEEK */
    struct {
        /** 'OR' list of jkk_schedule_days_t */
        uint8_t repeat_days;
    } day;
    /** For type JKK_SCHEDULE_TYPE_DATE */
    struct {
        /** Day of the month. Accepted values: 1-31. */
        uint8_t day;
        /* 'OR' list of jkk_schedule_months_t */
        uint16_t repeat_months;
        /** Year */
        uint16_t year;
        /** If the schedule is to be repeated every year. */
        bool repeat_every_year;
    } date;
    /** Relative offset in seconds for relative mode and sunrise/sunset +/- offset. */
    int relative_seconds;
    /** Cached next trigger timestamp in UTC seconds. */
    time_t next_scheduled_time_utc;
} jkk_schedule_trigger_t;

/** Optional validity window (UTC). */
typedef struct jkk_schedule_validity {
    /* Start time as UTC timestamp */
    time_t start_time;
    /* End time as UTC timestamp */
    time_t end_time;
} jkk_schedule_validity_t;

/** Complete schedule configuration. */
typedef struct jkk_schedule_config {
    /** Name of the schedule. This is like a primary key for the schedule. This is required. +1 for NULL termination. */
    char name[MAX_SCHEDULE_NAME_LEN + 1];
    bool enabled;
    /** Trigger details */
    jkk_schedule_trigger_t trigger;
    /** Trigger callback */
    jkk_schedule_trigger_cb_t trigger_cb;
    /** Timestamp callback */
    jkk_schedule_timestamp_cb_t timestamp_cb;
    /** Private data associated with the schedule. This will be passed to callbacks. */
    void *priv_data;
    uint32_t priv_param;
    /** Validity of schedules. */
    jkk_schedule_validity_t validity;
} jkk_schedule_config_t;

typedef struct jkk_schedules_handle {
    jkk_schedule_handle_t handle_list[MAX_SCHEDULE_NUMBER];
    jkk_schedule_handle_t next_handler;
    time_t next_min_scheduled_time_utc;
    TimerHandle_t timer;
    char *schedule_nvs_namespace;
    bool nvs_enabled;
    char *schedule_nvs_partition;
    int32_t latitude_e5;
    int32_t longitude_e5;
} jkk_schedules_handle_t;

typedef struct JkkSchedule {
    char name[MAX_SCHEDULE_NAME_LEN + 1];
    bool enabled;
    jkk_schedule_trigger_t trigger;
    uint32_t next_scheduled_time_diff;
    jkk_schedules_handle_t *parent;   /* back-pointer to owning handle (set by jkk_schedule_add / jkk_schedule_run_all) */
    jkk_schedule_trigger_cb_t trigger_cb;
    jkk_schedule_timestamp_cb_t timestamp_cb;
    void *priv_data;
    uint32_t priv_param;
    jkk_schedule_validity_t validity;
} jkk_schedule_t;


/**
 * @brief Create and initialize the schedule manager.
 *
 * This function allocates and initializes a schedule manager handle.
 * It does not automatically run schedules. Call jkk_schedule_run_all() after
 * loading or adding schedules.
 *
 * @param[in] nvs_partition Optional NVS partition name. Pass NULL for default.
 * @param[in] nvs_namespace Optional NVS namespace. Pass NULL for default "schd".
 * @param[in] latitude_e5 Latitude multiplied by 100000 (example: 5212345).
 * @param[in] longitude_e5 Longitude multiplied by 100000 (example: 2101234).
 *
 * @return Pointer to initialized manager, or NULL on allocation error.
 */
jkk_schedules_handle_t *jkk_schedule_init(char *nvs_partition, char *nvs_namespace, int32_t latitude_e5, int32_t longitude_e5);

/**
 * @brief Compute sunrise or sunset in local minutes from midnight.
 */
int16_t JkkSuntime(int16_t offset, bool sunRise, int16_t day, int16_t month, int16_t year, float latitude, float longitude);
/**
 * @brief Compute sunrise or sunset with timezone offset derived from localtime.
 */
int16_t JkkSuntimeAutoZone(time_t timeS, bool sunRise, int16_t day, int16_t month, int16_t year, float latitude, float longitude);
/**
 * @brief Compute sunrise or sunset for current date based on timestamp.
 */
int16_t JkkSuntimeNow(time_t now, bool sunRise, float latitude, float longitude);

/** @brief Load all schedules from NVS into the handle list. */
esp_err_t jkk_schedule_get_all(jkk_schedules_handle_t *schedules_h);
/** @brief Set callback and private data for all loaded schedules. */
esp_err_t jkk_schedule_callback_all(jkk_schedules_handle_t *schedules_h, jkk_schedule_trigger_cb_t trigger_cb, void *priv_data);
/** @brief Set callback and private data for a single schedule handle. */
esp_err_t jkk_schedule_callback_add(jkk_schedule_handle_t schedule_h, jkk_schedule_trigger_cb_t trigger_cb, void *priv_data);
/** @brief Remove expired one-time schedules from manager and NVS. */
esp_err_t jkk_schedule_clean(jkk_schedules_handle_t *schedules_h);
/** @brief Start scheduler runtime and arm shared master timer. */
esp_err_t jkk_schedule_run_all(jkk_schedules_handle_t *schedules_h, bool runMissed);
/** @brief Add a created schedule handle to manager list. */
esp_err_t jkk_schedule_add(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t schedule);
/** @brief Remove schedule handle from manager list without deleting storage. */
esp_err_t jkk_schedule_remove(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t schedule);
/** @brief Find schedule handle by name. Returns NULL when not found. */
jkk_schedule_handle_t jkk_schedule_find(jkk_schedules_handle_t *schedules_h, char *name);
/** @brief Count valid schedule handles currently registered in manager. */
int8_t jkk_schedule_count(jkk_schedules_handle_t *schedules_h);

/**
 * @brief Create a schedule object from configuration.
 *
 * The returned handle is not active until added to manager and runtime is started.
 *
 * @param[in] schedules_h Manager handle used for persistence context.
 * @param[in] schedule_config Full schedule configuration.
 *
 * @return Valid schedule handle on success, NULL on error.
 */
jkk_schedule_handle_t jkk_schedule_create(jkk_schedules_handle_t *schedules_h, jkk_schedule_config_t *schedule_config);

/** @brief Delete a schedule object and remove it from NVS storage. */
esp_err_t jkk_schedule_delete(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t handle);

/**
 * @brief Edit an existing schedule in memory and NVS.
 *
 * Schedule name in @p schedule_config must match the existing handle name.
 */
esp_err_t jkk_schedule_edit(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t handle, jkk_schedule_config_t *schedule_config);

/** @brief Enable a schedule and schedule its next trigger timestamp. */
esp_err_t jkk_schedule_enable(jkk_schedule_handle_t handle);

/** @brief Disable a schedule without removing it from manager or NVS. */
esp_err_t jkk_schedule_disable(jkk_schedule_handle_t handle);

/** @brief Read current schedule state into user-provided config structure. */
esp_err_t jkk_schedule_get(jkk_schedule_handle_t handle, jkk_schedule_config_t *schedule_config);

#ifdef __cplusplus
}
#endif
