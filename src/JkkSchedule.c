/* JkkSchedule - Lightweight scheduling library for ESP-IDF
 * Copyright (C) 2025 Jaromir Kopp (JKK)
 * Core scheduler logic, timer orchestration and sunrise/sunset integration
 *
 * Inspired by the original Espressif esp_schedule component and significantly
 * redesigned with multiple fixes and behavior improvements.
 */


#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>

#include "JkkScheduleInternal.h"

static const char *TAG = "JkkSchedule";

#define SECONDS_TILL_2020 ((2020 - 1970) * 365 * 24 * 3600)
#define SECONDS_IN_DAY (60 * 60 * 24)

/** Maximum period for the master FreeRTOS timer [s].
 *  Capped at 61 minutes so that a DST change (+/-1 h) always causes a
 *  re-evaluation within at most one hour, preventing schedules from firing
 *  at the wrong wall-clock time after a DST transition. */
#define JKK_SCHEDULE_MAX_TIMER_S    3660u
/** Tolerance [s]: fire a schedule if we are within this many seconds *before*
 *  the target time (handles FreeRTOS timer tick rounding). */
#define JKK_SCHEDULE_TRIGGER_TOLE_S 30u

#define SCHEDULE_NVS_NAMESPACE "schd"

#define JKK_SCHEDULE_ZENITH 90.833333333

static bool jkk_schedule_geo_is_valid(int32_t latitude_e5, int32_t longitude_e5)
{
    return latitude_e5 >= -9000000 && latitude_e5 <= 9000000 && longitude_e5 >= -18000000 && longitude_e5 <= 18000000;
}

int16_t JkkSuntimeNow(time_t now, bool sunRise, float latitude, float longitude)
{
    struct tm timeinfo = { 0 };
    localtime_r(&now, &timeinfo);
    int16_t timeZone = (((timeinfo.tm_hour * 60) + (timeinfo.tm_min)) - ((((now % (24 * 60 * 60)) / (60 * 60)) * 60) + ((now % (60 * 60)) / 60))) - timeinfo.tm_isdst * 60;
    return JkkSuntime(timeZone, sunRise, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, latitude, longitude);
}

int16_t JkkSuntimeAutoZone(time_t timeS, bool sunRise, int16_t day, int16_t month, int16_t year, float latitude, float longitude)
{
    struct tm timeinfo = { 0 };

    if (timeS < (time_t)JKK_ERA) {
        time(&timeS);
    }
    localtime_r(&timeS, &timeinfo);
    int16_t timeZone = (((timeinfo.tm_hour * 60) + (timeinfo.tm_min)) - ((((timeS % (24 * 60 * 60)) / (60 * 60)) * 60) + ((timeS % (60 * 60)) / 60)));

    return JkkSuntime(timeZone, sunRise, day, month, year, latitude, longitude);
}

int16_t JkkSuntime(int16_t offset, bool sunRise, int16_t day, int16_t month, int16_t year, float latitude, float longitude)
{
    float lngHour, time, m, el, ra, lQuadrant, raQuadrant, sinDec;
    float cosDec, cosH, h, t, ut, localT;
    int16_t n1, n2, n3, n0;

    n1 = floor(275 * month / 9);
    n2 = floor((month + 9) / 12);
    n3 = (1 + floor((year - 4 * floor(year / 4) + 2) / 3));
    n0 = n1 - (n2 * n3) + day - 30;
    lngHour = longitude / 15;
    if (sunRise) {
        time = n0 + ((6 - lngHour) / 24);
    } else {
        time = n0 + ((18 - lngHour) / 24);
    }
    m = (0.9856 * time) - 3.289;
    el = m + (1.916 * sin((M_PI / 180) * m)) + (0.020 * sin((M_PI / 180) * 2 * m)) + 282.634;
    while ((el > 360) || (el < 0)) {
        if (el > 360) el -= 360;
        if (el < 0) el += 360;
    }
    ra = (180 / M_PI) * atan(0.91764 * tan((M_PI / 180) * el));
    while ((ra > 360) || (ra < 0)) {
        if (ra > 360) ra -= 360;
        if (ra < 0) ra += 360;
    }
    lQuadrant = (floor(el / 90)) * 90;
    raQuadrant = (floor(ra / 90)) * 90;
    ra = ra + (lQuadrant - raQuadrant);
    ra = ra / 15;
    sinDec = 0.39782 * sin((M_PI / 180) * el);
    cosDec = cos((M_PI / 180) * ((180 / M_PI) * asin(sinDec)));
    cosH = (cos((M_PI / 180) * JKK_SCHEDULE_ZENITH) - (sinDec * sin((M_PI / 180) * latitude))) / (cosDec * cos((M_PI / 180) * latitude));
    if (cosH > 1) {
        return INT16_MAX;
    } else if (cosH < -1) {
        return INT16_MIN;
    }
    if (sunRise) {
        h = 360 - ((180 / M_PI) * acos(cosH));
    } else {
        h = (180 / M_PI) * acos(cosH);
    }
    h = h / 15;
    t = h + ra - (0.06571 * time) - 6.622;
    ut = t - lngHour;
    localT = ut + (float)offset / 60.0;

    while ((localT > 24) || (localT < 0)) {
        if (localT > 24) localT -= 24;
        if (localT < 0) localT += 24;
    }

    float fSunRiseHour = floor(localT);
    int8_t iSunRiseHour = (int8_t)(fSunRiseHour);
    float fSunRiseMinute = (localT - fSunRiseHour) * 60;
    int8_t iSunRiseMinute = (int8_t)(fSunRiseMinute);

    return iSunRiseHour * 60 + iSunRiseMinute;
}

static int jkk_schedule_get_no_of_days(jkk_schedule_trigger_t *trigger, struct tm *current_time, struct tm *schedule_time){
    /* for day, monday = 0, sunday = 6. */
    int next_day = 0;
    /* struct tm has tm_wday with sunday as 0. Whereas we have monday as 0. Converting struct tm to our format */
    int today = ((current_time->tm_wday + 7 - 1) % 7);

    jkk_schedule_days_t today_bit = 1 << today;
    uint8_t repeat_days = trigger->day.repeat_days;
    int current_seconds = (current_time->tm_hour * 60 + current_time->tm_min) * 60 + current_time->tm_sec;
    int schedule_seconds = (schedule_time->tm_hour * 60 + schedule_time->tm_min) * 60;

    /* Handling for one time schedule */
    if (repeat_days == JKK_SCHEDULE_DAY_ONCE) {
        if (schedule_seconds > current_seconds) {
            /* The schedule is today and is yet to go off */
            return 0;
        } else {
            /* The schedule is tomorrow */
            return 1;
        }
    }

    /* Handling for repeating schedules */
    /* Check if it is today */
    if ((repeat_days & today_bit)) {
        if (schedule_seconds > current_seconds) {
            /* The schedule is today and is yet to go off. */
            return 0;
        }
    }
    /* Check if it is this week or next week */
    if ((repeat_days & (today_bit ^ 0xFF)) > today_bit) {
        /* Next schedule is yet to come in this week */
        next_day = ffs(repeat_days & (0xFF << (today + 1))) - 1;
        return (next_day - today);
    } else {
        /* First scheduled day of the next week */
        next_day = ffs(repeat_days) - 1;
        if (next_day == today) {
            /* Same day, next week */
            return 7;
        }
        return (7 - today + next_day);
    }

    ESP_LOGE(TAG, "No of days could not be found. This should not happen.");
    return 0;
}

static uint8_t jkk_schedule_get_next_month(jkk_schedule_trigger_t *trigger, struct tm *current_time, struct tm *schedule_time){
    int current_seconds = (current_time->tm_hour * 60 + current_time->tm_min) * 60 + current_time->tm_sec;
    int schedule_seconds = (schedule_time->tm_hour * 60 + schedule_time->tm_min) * 60;
    /* +1 is because struct tm has months starting from 0, whereas we have them starting from 1 */
    uint8_t current_month = current_time->tm_mon + 1;
    /* -1 because month_bit starts from 0b1. So for January, it should be 1 << 0. And current_month starts from 1. */
    uint16_t current_month_bit = 1 << (current_month - 1);
    uint8_t next_schedule_month = 0;
    uint16_t repeat_months = trigger->date.repeat_months;

    /* Check if month is not specified */
    if (repeat_months == JKK_SCHEDULE_MONTH_ONCE) {
        if (trigger->date.day == current_time->tm_mday) {
            /* The schedule day is same. Check if time has already passed */
            if (schedule_seconds > current_seconds) {
                /* The schedule is today and is yet to go off */
                return current_month;
            } else {
                /* Today's time has passed */
                return (current_month + 1);
            }
        } else if (trigger->date.day > current_time->tm_mday) {
            /* The day is yet to come in this month */
            return current_month;
        } else {
            /* The day has passed in the current month */
            return (current_month + 1);
        }
    }

    /* Check if schedule is not this year itself, it is in future. */
    if (trigger->date.year > (current_time->tm_year + 1900)) {
        /* Find first schedule month of next year */
        next_schedule_month = ffs(repeat_months);
        /* Year will be handled by the caller. So no need to add any additional months */
        return next_schedule_month;
    }

    /* Check if schedule is this month and is yet to come */
    if (current_month_bit & repeat_months) {
        if (trigger->date.day == current_time->tm_mday) {
            /* The schedule day is same. Check if time has already passed */
            if (schedule_seconds > current_seconds) {
                /* The schedule is today and is yet to go off */
                return current_month;
            }
        }
        if (trigger->date.day > current_time->tm_mday) {
            /* The day is yet to come in this month */
            return current_month;
        }
    }

    /* Check if schedule is this year */
    if ((repeat_months & (current_month_bit ^ 0xFFFF)) > current_month_bit) {
        /* Next schedule month is yet to come in this year */
        next_schedule_month = ffs(repeat_months & (0xFFFF << (current_month)));
        return next_schedule_month;
    }

    /* Check if schedule is for this year and does not repeat */
    if (!trigger->date.repeat_every_year) {
        if (trigger->date.year <= (current_time->tm_year + 1900)) {
            ESP_LOGE(TAG, "Schedule does not repeat next year, but get_next_month has been called.");
            return 0;
        }
    }

    /* Schedule is not this year */
    /* Find first schedule month of next year */
    next_schedule_month = ffs(repeat_months);
    /* +12 because the schedule is next year */
    return (next_schedule_month + 12);
}

static uint16_t jkk_schedule_get_next_year(jkk_schedule_trigger_t *trigger, struct tm *current_time, struct tm *schedule_time){
    uint16_t current_year = current_time->tm_year + 1900;
    uint16_t schedule_year = trigger->date.year;
    if (schedule_year > current_year) {
        return schedule_year;
    }
    /* If the schedule is set to repeat_every_year, we return the current year */
    /* If the schedule has already passed in this year, we still return current year, as the additional months will be handled in get_next_month */
    return current_year;
}

static uint32_t jkk_schedule_get_next_time_diff(const char *schedule_name, jkk_schedule_trigger_t *trigger, struct tm *current_time, bool noSun, int32_t latitude_e5, int32_t longitude_e5){
    struct tm schedule_time;

    time_t now = mktime(current_time);
    char time_str[64];
    int32_t time_diff;

    /* Get current time */
 //   time(&now);
    /* Handling JKK_SCHEDULE_TYPE_RELATIVE first since it doesn't require any
     * computation based on days, hours, minutes, etc.
     */
    if (trigger->type == JKK_SCHEDULE_TYPE_RELATIVE) {
        /* If next scheduled time is already set, just compute the difference
         * between current time and next scheduled time and return that diff.
         */
        time_t target;
        if (trigger->next_scheduled_time_utc > 0) {
            target = (time_t)trigger->next_scheduled_time_utc;
            time_diff = difftime(target, now);
            if(time_diff <= 0){
                if(time_diff < 0){
                    time_diff *= -1;
                    uint32_t steps = time_diff / trigger->relative_seconds;
                    target = (time_t)(steps * trigger->relative_seconds) + (time_t)trigger->next_scheduled_time_utc;
                    ESP_LOGI(TAG, "Schedule rel Step: %ld", steps);
                }
                target += trigger->relative_seconds;
                time_diff = difftime(target, now);
                ESP_LOGI(TAG, "Schedule rel %ld, Now: %ld, target: %ld, time_diff: %ld", (uint32_t)now, (uint32_t)target, time_diff);
            }
        } else {
            target = now + (time_t)trigger->relative_seconds;
            time_diff = trigger->relative_seconds;
        }
        localtime_r(&target, &schedule_time);
        trigger->next_scheduled_time_utc = mktime(&schedule_time);
        /* Print schedule time */
        memset(time_str, 0, sizeof(time_str));
        strftime(time_str, sizeof(time_str), "%c %z[%Z]", &schedule_time);
        ESP_LOGI(TAG, "Schedule rel %s test: %s. DST: %s. Next_scheduled_time_utc %ld", schedule_name, time_str, schedule_time.tm_isdst ? "Yes" : "No", trigger->next_scheduled_time_utc);
        return time_diff;
    }
 //   localtime_r(&now, &current_time);

    /* Get schedule time */
    localtime_r(&now, &schedule_time);
    schedule_time.tm_sec = 0;
    schedule_time.tm_min = trigger->type >= JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE && trigger->type <= JKK_SCHEDULE_TYPE_DATE_SUNSET && !noSun ? schedule_time.tm_min + 1 : trigger->minutes;
    schedule_time.tm_hour = trigger->type >= JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE && trigger->type <= JKK_SCHEDULE_TYPE_DATE_SUNSET && !noSun ? schedule_time.tm_hour : trigger->hours;

    mktime(&schedule_time);

    /* Adjust schedule day */
    int no_of_days = -1;
    if (trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK || trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE || trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET) {
        no_of_days = 0;
    //    ESP_LOGW(TAG, "jkk_schedule_get_no_of_days: schedule_time.tm_sec: %d, schedule_time.tm_min: %d, schedule_time.tm_hour: %d", schedule_time.tm_sec, schedule_time.tm_min, schedule_time.tm_hour);
        no_of_days = jkk_schedule_get_no_of_days(trigger, current_time, &schedule_time);
    //    ESP_LOGW(TAG, "jkk_schedule_get_no_of_days: %d, current_time: %lld, schedule_time: %lld", no_of_days, (int64_t)mktime(current_time), (int64_t)mktime(&schedule_time));
        schedule_time.tm_sec += no_of_days * SECONDS_IN_DAY;
    }
    if (trigger->type == JKK_SCHEDULE_TYPE_DATE || trigger->type == JKK_SCHEDULE_TYPE_DATE_SUNRISE || trigger->type == JKK_SCHEDULE_TYPE_DATE_SUNSET) {
        schedule_time.tm_mday = trigger->date.day;
        schedule_time.tm_mon = jkk_schedule_get_next_month(trigger, current_time, &schedule_time) - 1;
        schedule_time.tm_year = jkk_schedule_get_next_year(trigger, current_time, &schedule_time) - 1900;
        if (schedule_time.tm_mon < 0) {
            ESP_LOGE(TAG, "Invalid month found: %d. Setting it to next month.", schedule_time.tm_mon);
            schedule_time.tm_mon = current_time->tm_mon + 1;
        }
        if (schedule_time.tm_mon >= 12) {
            schedule_time.tm_year += schedule_time.tm_mon / 12;
            schedule_time.tm_mon = schedule_time.tm_mon % 12;
        }
    }

    if (trigger->type >= JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE && trigger->type <= JKK_SCHEDULE_TYPE_DATE_SUNSET && !noSun) {
        schedule_time.tm_hour = 0;
        schedule_time.tm_min = 0;
        mktime(&schedule_time);
        int16_t sun = JkkSuntimeAutoZone(mktime(&schedule_time), trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE || trigger->type == JKK_SCHEDULE_TYPE_DATE_SUNRISE, schedule_time.tm_mday, schedule_time.tm_mon + 1, schedule_time.tm_year + 1900, latitude_e5 / 100000.0f, longitude_e5 / 100000.0f);
        if(sun != INT16_MAX && sun != INT16_MIN) {
            schedule_time.tm_sec = (sun * 60 ) + trigger->relative_seconds;
        }
        else {
            schedule_time.tm_sec = 0;
        }
        ESP_LOGE(TAG, "Sunset: %d, %02d.%02d, %d", sun, sun / 60, sun - ((sun / 60) * 60), trigger->relative_seconds);
        /* Compare against effective trigger time (sun ± relative_seconds), not just raw sun time.
         * Without this, a negative relative_seconds offset (e.g. -900 for "15 min before sunrise")
         * would cause a negative time_diff when the schedule is evaluated after the effective trigger
         * time but before actual sunrise, making the fallback fixed-time fire instead. */
        int _eff_sec = (int)sun * 60 + trigger->relative_seconds;
        int _cur_sec = current_time->tm_hour * 3600 + current_time->tm_min * 60 + current_time->tm_sec;
        if(no_of_days == 0 && _cur_sec >= _eff_sec){
            schedule_time.tm_sec += 1 * SECONDS_IN_DAY;
        }
    }

    /* Adjust time according to DST */
    time_t dst_adjust = 0;
    if (!current_time->tm_isdst && schedule_time.tm_isdst) {
        dst_adjust = -3600;
    } else if (current_time->tm_isdst && !schedule_time.tm_isdst ) {
        dst_adjust = 3600;
    }
    ESP_LOGD(TAG, "DST adjust seconds: %lld", (long long) dst_adjust);
    schedule_time.tm_sec += dst_adjust;

    mktime(&schedule_time);

    /* Calculate difference */
    time_diff = difftime((mktime(&schedule_time)), mktime(current_time));

    /* For one time schedules to check for expiry after a reboot. If NVS is enabled, this should be stored in NVS. */
    trigger->next_scheduled_time_utc = mktime(&schedule_time);

    return time_diff;
}

static uint32_t jkk_schedule_get_next_schedule_time_diff(const char *schedule_name, jkk_schedule_trigger_t *trigger, struct tm *current_time, int32_t latitude_e5, int32_t longitude_e5){

    uint32_t sun = 0;
    uint32_t hours = 0;
    switch (trigger->type){
        case JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE:{
            if(trigger->hours < 24 && trigger->minutes < 60){
                if (!jkk_schedule_geo_is_valid(latitude_e5, longitude_e5)) {
                    return jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
                }
                sun = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, false, latitude_e5, longitude_e5);
                hours = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
            //    printf("SUNRISE sun < hours: %d Sun: %ld, hours: %ld %s\n", sun < hours, sun, hours, schedule_name);
                return sun < hours ? sun : hours;
            }
        }
        break;
        case JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET:{
            if(trigger->hours < 24 && trigger->minutes < 60){
                if (!jkk_schedule_geo_is_valid(latitude_e5, longitude_e5)) {
                    return jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
                }
                sun = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, false, latitude_e5, longitude_e5);
                hours = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
            //    printf("SUNSET sun > hours: %d Sun: %ld, hours: %ld %s\n", sun > hours, sun, hours, schedule_name);
                return sun > hours ? sun : hours;
            }
        }
        break;
        case JKK_SCHEDULE_TYPE_DATE_SUNRISE:{
            if(trigger->hours < 24 && trigger->minutes < 60){
                if (!jkk_schedule_geo_is_valid(latitude_e5, longitude_e5)) {
                    return jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
                }
                sun = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, false, latitude_e5, longitude_e5);
                hours = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
                return sun < hours ? sun : hours;
            }
        }
        break;
        case JKK_SCHEDULE_TYPE_DATE_SUNSET:{
            if(trigger->hours < 24 && trigger->minutes < 60){
                if (!jkk_schedule_geo_is_valid(latitude_e5, longitude_e5)) {
                    return jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
                }
                sun = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, false, latitude_e5, longitude_e5);
                hours = jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, true, latitude_e5, longitude_e5);
                return sun > hours ? sun : hours;
            }
        }
        default:
        break;
    }
    return jkk_schedule_get_next_time_diff(schedule_name, trigger, current_time, false, latitude_e5, longitude_e5);
}

static bool jkk_schedule_is_expired(jkk_schedule_trigger_t *trigger){
    time_t current_timestamp = 0;
    struct tm current_time = {0};
    time(&current_timestamp);
    localtime_r(&current_timestamp, &current_time);

    if (trigger->type == JKK_SCHEDULE_TYPE_RELATIVE) {
        if (trigger->next_scheduled_time_utc == 0) {
            /* Schedule has been disabled , so it is as good as expired. */
            return true;
        }
    } else if (trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK || trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE || trigger->type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET) {
        if (trigger->day.repeat_days == JKK_SCHEDULE_DAY_ONCE) {
            if (trigger->next_scheduled_time_utc > 0 && trigger->next_scheduled_time_utc <= current_timestamp) {
                /* One time schedule has expired */
                return true;
            } else if (trigger->next_scheduled_time_utc == 0) {
                /* Schedule has been disabled , so it is as good as expired. */
                return true;
            }
        }
    } else if (trigger->type == JKK_SCHEDULE_TYPE_DATE || trigger->type == JKK_SCHEDULE_TYPE_DATE_SUNRISE || trigger->type == JKK_SCHEDULE_TYPE_DATE_SUNSET) {
        if (trigger->date.repeat_months == 0) {
            if (trigger->next_scheduled_time_utc > 0 && trigger->next_scheduled_time_utc <= current_timestamp) {
                /* One time schedule has expired */
                return true;
            } else {
                return false;
            }
        }
        if (trigger->date.repeat_every_year == true) {
            return false;
        }

        struct tm schedule_time = {0};
        localtime_r(&current_timestamp, &schedule_time);
        schedule_time.tm_sec = 0;
        schedule_time.tm_min = trigger->minutes;
        schedule_time.tm_hour = trigger->hours;
        schedule_time.tm_mday = trigger->date.day;
        /* For expiry, just check the last month of the repeat_months. */
        /* '-1' because struct tm has months starting from 0 and we have months starting from 1. */
        schedule_time.tm_mon = fls(trigger->date.repeat_months) - 1;
        /* '-1900' because struct tm has number of years after 1900 */
        schedule_time.tm_year = trigger->date.year - 1900;
        time_t schedule_timestamp = mktime(&schedule_time);

        if (schedule_timestamp < current_timestamp) {
            return true;
        }
    } else {
        /* Invalid type. Mark as expired */
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * Single shared timer management
 * -------------------------------------------------------------------------*/

/** Finds the nearest next_scheduled_time_utc among all enabled schedules and
 *  arms the single master FreeRTOS timer accordingly.
 *  Capped at JKK_SCHEDULE_MAX_TIMER_S so DST transitions are handled within
 *  at most one hour. */
static void jkk_schedule_restart_master_timer(jkk_schedules_handle_t *schedules_h)
{
    if (!schedules_h || !schedules_h->timer) return;

    time_t now = 0;
    time(&now);
    if (now < SECONDS_TILL_2020) {
        ESP_LOGE(TAG, "Time not set — master timer not started");
        return;
    }

    time_t nearest = 0;
    for (int i = 0; i < MAX_SCHEDULE_NUMBER; i++) {
        jkk_schedule_t *s = (jkk_schedule_t *)schedules_h->handle_list[i];
        if (!s || !s->enabled) continue;
        if (s->trigger.next_scheduled_time_utc == 0) continue;
        if (nearest == 0 || s->trigger.next_scheduled_time_utc < nearest) {
            nearest = s->trigger.next_scheduled_time_utc;
        }
    }
    schedules_h->next_min_scheduled_time_utc = nearest;

    uint32_t delay_s;
    if (nearest == 0) {
        delay_s = JKK_SCHEDULE_MAX_TIMER_S;
    } else {
        int32_t diff = (int32_t)difftime(nearest, now);
        if (diff <= 0) diff = 1;
        if ((uint32_t)diff > JKK_SCHEDULE_MAX_TIMER_S) diff = (int32_t)JKK_SCHEDULE_MAX_TIMER_S;
        delay_s = (uint32_t)diff;
    }

    xTimerStop(schedules_h->timer, portMAX_DELAY);
    xTimerChangePeriod(schedules_h->timer, (delay_s * 1000) / portTICK_PERIOD_MS, portMAX_DELAY);
    ESP_LOGI(TAG, "Master timer: %"PRIu32" s (nearest utc: %ld)", delay_s, (long)nearest);
}

static void jkk_schedule_stop_timer(jkk_schedule_t *schedule){
    if (schedule->parent) {
        jkk_schedule_restart_master_timer(schedule->parent);
    }
}

static void jkk_schedule_start_timer(jkk_schedule_t *schedule){
    time_t now = 0;
    time(&now);
    if (now < SECONDS_TILL_2020) {
        ESP_LOGE(TAG, "Time is not updated");
        return;
    }

    struct tm current_time = {0};
    localtime_r(&now, &current_time);

    int32_t latitude_e5 = 0;
    int32_t longitude_e5 = 0;
    if (schedule->parent) {
        latitude_e5 = schedule->parent->latitude_e5;
        longitude_e5 = schedule->parent->longitude_e5;
    }
    schedule->next_scheduled_time_diff = jkk_schedule_get_next_schedule_time_diff(schedule->name, &schedule->trigger, &current_time, latitude_e5, longitude_e5);

    time_t next_t = now + schedule->next_scheduled_time_diff;
    struct tm next_tm = {0};
    localtime_r(&next_t, &next_tm);

    /* Print schedule time */
    char time_str[64];
    memset(time_str, 0, sizeof(time_str));
    strftime(time_str, sizeof(time_str), "%c %z[%Z]", &next_tm);
    ESP_LOGW(TAG, "Schedule %s will be active on: %s. DST: %s", schedule->name, time_str, next_tm.tm_isdst ? "Yes" : "No");

    ESP_LOGI(TAG, "Next trigger for schedule %s in %"PRIu32" seconds", schedule->name, schedule->next_scheduled_time_diff);

    if (schedule->timestamp_cb) {
        schedule->timestamp_cb((jkk_schedule_handle_t)schedule, schedule->trigger.next_scheduled_time_utc, schedule->priv_data);
    }

    /* Arm the shared master timer for this schedules_handle */
    if (schedule->parent) {
        jkk_schedule_restart_master_timer(schedule->parent);
    }
}

static void jkk_schedule_master_timer_cb(TimerHandle_t timer){
    jkk_schedules_handle_t *schedules_h = (jkk_schedules_handle_t *)pvTimerGetTimerID(timer);
    if (!schedules_h) return;

    time_t now = 0;
    time(&now);

    for (int i = 0; i < MAX_SCHEDULE_NUMBER; i++) {
        jkk_schedule_t *schedule = (jkk_schedule_t *)schedules_h->handle_list[i];
        if (!schedule || !schedule->enabled) continue;
        if (schedule->trigger.next_scheduled_time_utc == 0) continue;

        /* Fire only if within tolerance before the target, or already past it */
        int32_t diff = (int32_t)difftime(now, (time_t)schedule->trigger.next_scheduled_time_utc);
        if (diff < -(int32_t)JKK_SCHEDULE_TRIGGER_TOLE_S) continue;

        bool skip_trigger = false;
        struct tm validity_time;
        char time_str[64] = {0};

        /* Validity window: start */
        if (schedule->validity.start_time != 0 && now < schedule->validity.start_time) {
            memset(time_str, 0, sizeof(time_str));
            localtime_r(&schedule->validity.start_time, &validity_time);
            strftime(time_str, sizeof(time_str), "%c %z[%Z]", &validity_time);
            ESP_LOGW(TAG, "Schedule %s skipped. It will be active only after: %s. DST: %s.",
                     schedule->name, time_str, validity_time.tm_isdst ? "Yes" : "No");
            skip_trigger = true;
        }

        /* Validity window: end */
        if (!skip_trigger && schedule->validity.end_time != 0 && now > schedule->validity.end_time) {
            localtime_r(&schedule->validity.end_time, &validity_time);
            strftime(time_str, sizeof(time_str), "%c %z[%Z]", &validity_time);
            ESP_LOGW(TAG, "Schedule %s skipped. It can't be active after: %s. DST: %s.",
                     schedule->name, time_str, validity_time.tm_isdst ? "Yes" : "No");
            schedule->enabled = false;
            continue;  /* expired — do not reschedule */
        }

        /* Sunrise/sunset sanity check */
        if (!skip_trigger &&
            schedule->trigger.type >= JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE &&
            schedule->trigger.type <= JKK_SCHEDULE_TYPE_DATE_SUNSET) {
            localtime_r(&now, &validity_time);
            if (jkk_schedule_geo_is_valid(schedules_h->latitude_e5, schedules_h->longitude_e5)) {
                int16_t sun = JkkSuntimeAutoZone(now,
                    schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE ||
                    schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE_SUNRISE,
                    validity_time.tm_mday, validity_time.tm_mon + 1, validity_time.tm_year + 1900,
                    schedules_h->latitude_e5 / 100000.0f, schedules_h->longitude_e5 / 100000.0f);
                if (sun == INT16_MAX || sun == INT16_MIN) {
                    skip_trigger = true;
                }
            }
        }

        if (!skip_trigger) {
            ESP_LOGI(TAG, "Schedule %s triggered", schedule->name);
            if (schedule->trigger_cb) {
                schedule->trigger_cb((jkk_schedule_handle_t)schedule, schedule->priv_data);
            }
        }

        /* Reschedule unless one-time schedule has expired */
        if (jkk_schedule_is_expired(&schedule->trigger)) {
            /* Not deleting the schedule here. Just not starting it again. */
            schedule->enabled = false;
            continue;
        }

        {
            struct tm current_time = {0};
            localtime_r(&now, &current_time);
            schedule->next_scheduled_time_diff = jkk_schedule_get_next_schedule_time_diff(
                schedule->name, &schedule->trigger, &current_time, schedules_h->latitude_e5, schedules_h->longitude_e5);
            time_t next_t = (time_t)schedule->trigger.next_scheduled_time_utc;
            struct tm next_tm = {0};
            localtime_r(&next_t, &next_tm);
            memset(time_str, 0, sizeof(time_str));
            strftime(time_str, sizeof(time_str), "%c %z[%Z]", &next_tm);
            ESP_LOGW(TAG, "Schedule %s rescheduled: %s. DST: %s",
                     schedule->name, time_str, next_tm.tm_isdst ? "Yes" : "No");
            if (schedule->timestamp_cb) {
                schedule->timestamp_cb((jkk_schedule_handle_t)schedule,
                    schedule->trigger.next_scheduled_time_utc, schedule->priv_data);
            }
        }
    }

    jkk_schedule_restart_master_timer(schedules_h);
}

static void jkk_schedule_create_timer(jkk_schedule_t *schedule){
    time_t now = 0;
    time(&now);
    if(now > JKK_ERA) {
        struct tm current_time = {0};
        localtime_r(&now, &current_time);
        schedule->next_scheduled_time_diff = jkk_schedule_get_next_schedule_time_diff(schedule->name, &schedule->trigger, &current_time, 0, 0);
    }
    /* Master timer is managed by jkk_schedules_handle_t — no per-schedule FreeRTOS timer. */
}

esp_err_t jkk_schedule_get(jkk_schedule_handle_t handle, jkk_schedule_config_t *schedule_config){
    if (schedule_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)handle;

    strcpy(schedule_config->name, schedule->name);
    schedule_config->trigger.type = schedule->trigger.type;
    schedule_config->trigger.hours = schedule->trigger.hours;
    schedule_config->trigger.minutes = schedule->trigger.minutes;
    if (schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK || schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE || schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET) {
        schedule_config->trigger.day.repeat_days = schedule->trigger.day.repeat_days;
    } else if (schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE || schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE_SUNRISE || schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE_SUNSET) {
        schedule_config->trigger.date.day = schedule->trigger.date.day;
        schedule_config->trigger.date.repeat_months = schedule->trigger.date.repeat_months;
        schedule_config->trigger.date.year = schedule->trigger.date.year;
        schedule_config->trigger.date.repeat_every_year = schedule->trigger.date.repeat_every_year;
    }
    schedule_config->enabled = schedule->enabled;
    schedule_config->trigger_cb = schedule->trigger_cb;
    schedule_config->timestamp_cb = schedule->timestamp_cb;
    schedule_config->priv_data = schedule->priv_data;
    schedule_config->priv_param = schedule->priv_param;
    schedule_config->validity = schedule->validity;
    return ESP_OK;
}

esp_err_t jkk_schedule_enable(jkk_schedule_handle_t handle){
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)handle;
    schedule->enabled = true;
    jkk_schedule_start_timer(schedule);
//    jkk_schedule_set(schedules_h, schedule, schedule_config);
    return ESP_OK;
}

esp_err_t jkk_schedule_disable(jkk_schedule_handle_t handle){
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)handle;
    schedule->enabled = false;
    jkk_schedule_stop_timer(schedule);
    /* Disabling a schedule should also reset the next_scheduled_time.
     * It would be re-computed after enabling.
     */
    schedule->trigger.next_scheduled_time_utc = 0;
    return ESP_OK;
}

static esp_err_t jkk_schedule_set(jkk_schedules_handle_t *schedules_h, jkk_schedule_t *schedule, jkk_schedule_config_t *schedule_config){
    /* Setting everything apart from name. */
    schedule->trigger.type = schedule_config->trigger.type;
    schedule->trigger.relative_seconds = schedule_config->trigger.relative_seconds;
    if (schedule->trigger.type == JKK_SCHEDULE_TYPE_RELATIVE) {
        schedule->trigger.next_scheduled_time_utc = schedule_config->trigger.next_scheduled_time_utc;
    } else {
        schedule->trigger.hours = schedule_config->trigger.hours;
        schedule->trigger.minutes = schedule_config->trigger.minutes;

        if (schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK || schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNRISE || schedule->trigger.type == JKK_SCHEDULE_TYPE_DAYS_OF_WEEK_SUNSET) {
            schedule->trigger.day.repeat_days = schedule_config->trigger.day.repeat_days;
        } else if (schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE || schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE_SUNRISE || schedule->trigger.type == JKK_SCHEDULE_TYPE_DATE_SUNSET) {
            schedule->trigger.date.day = schedule_config->trigger.date.day;
            schedule->trigger.date.repeat_months = schedule_config->trigger.date.repeat_months;
            schedule->trigger.date.year = schedule_config->trigger.date.year;
            schedule->trigger.date.repeat_every_year = schedule_config->trigger.date.repeat_every_year;
        }
    }
    schedule->enabled = schedule_config->enabled;
    schedule->trigger_cb = schedule_config->trigger_cb;
    schedule->timestamp_cb = schedule_config->timestamp_cb;
    schedule->priv_data = schedule_config->priv_data;
    schedule-> priv_param = schedule_config->priv_param;
    schedule->validity = schedule_config->validity;
    jkk_schedule_nvs_add(schedules_h, schedule);
    return ESP_OK;
}

esp_err_t jkk_schedule_edit(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t handle, jkk_schedule_config_t *schedule_config){
    if (handle == NULL || schedule_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)handle;
    if (strncmp(schedule->name, schedule_config->name, sizeof(schedule->name)) != 0) {
        ESP_LOGE(TAG, "Schedule name mismatch. Expected: %s, Passed: %s", schedule->name, schedule_config->name);
        return ESP_FAIL;
    }
    /* Editing a schedule with relative time should also reset it. */
//  if (schedule->trigger.type == JKK_SCHEDULE_TYPE_RELATIVE) {
  //      schedule->trigger.next_scheduled_time_utc = 0;
 // }
    jkk_schedule_set(schedules_h, schedule, schedule_config);
    ESP_LOGD(TAG, "Schedule %s edited", schedule->name);
    return ESP_OK;
}

esp_err_t jkk_schedule_delete(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t handle){
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)handle;
    ESP_LOGI(TAG, "Deleting schedule %s", schedule->name);
    schedule->enabled = false;
    if (schedule->parent) {
        jkk_schedule_restart_master_timer(schedule->parent);
    }
    jkk_schedule_nvs_remove(schedules_h, schedule);
    free(schedule);
    handle = NULL;
    return ESP_OK;
}

jkk_schedule_handle_t jkk_schedule_create(jkk_schedules_handle_t *schedules_h, jkk_schedule_config_t *schedule_config){
    if (schedule_config == NULL) {
        return NULL;
    }
    if (strlen(schedule_config->name) <= 0) {
        ESP_LOGE(TAG, "Set schedule failed. Please enter a unique valid name for the schedule.");
        return NULL;
    }

    if (schedule_config->trigger.type == JKK_SCHEDULE_TYPE_INVALID) {
        ESP_LOGE(TAG, "Schedule type is invalid.");
        return NULL;
    }

    jkk_schedule_t *schedule = (jkk_schedule_t *)calloc(1, sizeof(jkk_schedule_t));
    if (schedule == NULL) {
        ESP_LOGE(TAG, "Could not allocate handle");
        return NULL;
    }
    strlcpy(schedule->name, schedule_config->name, sizeof(schedule->name));

    jkk_schedule_set(schedules_h, schedule, schedule_config);

    jkk_schedule_create_timer(schedule);
    ESP_LOGD(TAG, "Schedule %s created", schedule->name);
    return (jkk_schedule_handle_t)schedule;
}

esp_err_t jkk_schedule_add(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t schedule_h){
    if (schedules_h == NULL || schedule_h == NULL) {
        ESP_LOGE(TAG, "jkk_schedule_add ESP_ERR_INVALID_ARG");
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    jkk_schedule_t *schedule = (jkk_schedule_t *)schedule_h;
    if(jkk_schedule_find(schedules_h, schedule->name)){
        ESP_LOGE(TAG, "jkk_schedule_add name exist");
        return ESP_FAIL;
    }
    for(uint8_t i = 0; i < MAX_SCHEDULE_NUMBER; i++){
        if(handle_list[i] == NULL){
            handle_list[i] = schedule;
            schedule->parent = schedules_h;
            jkk_schedule_restart_master_timer(schedules_h);
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "jkk_schedule_add ESP_ERR_NO_MEM");
    return ESP_ERR_NO_MEM;
}

esp_err_t jkk_schedule_remove(jkk_schedules_handle_t *schedules_h, jkk_schedule_handle_t schedule_h){
    if (schedules_h == NULL || schedule_h == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    for(uint8_t i = 0; i < MAX_SCHEDULE_NUMBER; i++){
        if(handle_list[i] == schedule_h){
            handle_list[i] = NULL;
            ret = ESP_OK;
        }
    }
    return ret;
}

jkk_schedule_handle_t jkk_schedule_find(jkk_schedules_handle_t *schedules_h, char *name){
    if (schedules_h == NULL || strlen(name) <= 0) {
        ESP_LOGE(TAG, "handle_list == NULL or bad name");
        return NULL;
    }
    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    jkk_schedule_t *schedule;
    for(uint8_t i = 0; i < MAX_SCHEDULE_NUMBER; i++){
        schedule = (jkk_schedule_t *)handle_list[i];
        if (schedule && strcmp(schedule->name, name) == 0){
            return handle_list[i];
        }
    }
    return NULL;
}

int8_t jkk_schedule_count(jkk_schedules_handle_t *schedules_h){
    if (schedules_h == NULL) {
        ESP_LOGE(TAG, "handle_list == NULL");
        return INT8_MIN;
    }
    int8_t schedules_nr = 0;
    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    for(uint8_t i = 0; i < MAX_SCHEDULE_NUMBER; i++){
        if (handle_list[i]){
            schedules_nr++;
        }
    }
    return schedules_nr;
}

jkk_schedules_handle_t *jkk_schedule_init(char *nvs_partition, char *nvs_namespace, int32_t latitude_e5, int32_t longitude_e5){
    jkk_schedules_handle_t *schedules_h = (jkk_schedules_handle_t *)calloc(1, sizeof(jkk_schedules_handle_t));

    if(schedules_h == NULL) {
        ESP_LOGE(TAG, "Can'assign memory to shedules");
        return NULL;
    }

    if (nvs_partition) {
        schedules_h->schedule_nvs_partition = strndup(nvs_partition, strlen(nvs_partition));
    } else {
        schedules_h->schedule_nvs_partition = strndup(NVS_DEFAULT_PART_NAME, strlen(NVS_DEFAULT_PART_NAME));
    }
    if (schedules_h->schedule_nvs_partition == NULL) {
        ESP_LOGE(TAG, "Could not allocate nvs_partition");
        return NULL;
    }

    ESP_LOGW(TAG, "nvs_namespace: %s/%d", nvs_namespace, strlen(nvs_namespace));
    if (nvs_namespace) {
        schedules_h->schedule_nvs_namespace = strndup(nvs_namespace, strlen(nvs_namespace));
        ESP_LOGE(TAG, "nvs_namespace1: %s", schedules_h->schedule_nvs_namespace);
    } else {
        schedules_h->schedule_nvs_namespace = strndup(SCHEDULE_NVS_NAMESPACE, strlen(SCHEDULE_NVS_NAMESPACE));
        ESP_LOGE(TAG, "nvs_namespace2: %s", schedules_h->schedule_nvs_namespace);
    }
    if (schedules_h->schedule_nvs_namespace == NULL) {
        ESP_LOGE(TAG, "Could not allocate nvs_namespace");
        return NULL;
    }
    schedules_h->nvs_enabled = true;
    schedules_h->latitude_e5 = latitude_e5;
    schedules_h->longitude_e5 = longitude_e5;

    ESP_LOGE(TAG, "nvs_namespace3: %s", schedules_h->schedule_nvs_namespace);

    return schedules_h;
}

esp_err_t jkk_schedule_get_all(jkk_schedules_handle_t *schedules_h){
    if(schedules_h == NULL){
        return ESP_ERR_INVALID_STATE;
    }

    /* Get handle list from NVS */
    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    int8_t schedule_count = jkk_schedule_nvs_get_all(schedules_h);
    if (schedule_count <= 0) {
        ESP_LOGE(TAG, "No schedules found in NVS or somethings wrong, err: %d", schedule_count);
        return schedule_count == 0 ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Schedules found in NVS: %"PRIu8, schedule_count);
    /* Start/Delete the schedules */
    for (size_t handle_count = 0; handle_count < schedule_count; handle_count++) {
        ((jkk_schedule_t *)(handle_list[handle_count]))->trigger_cb = NULL;
        ((jkk_schedule_t *)(handle_list[handle_count]))->parent = NULL;
        ESP_LOGI(TAG, "Schedule name: %s", ((jkk_schedule_t *)(handle_list[handle_count]))->name);
    }
    return ESP_OK;
}

esp_err_t jkk_schedule_callback_all(jkk_schedules_handle_t *schedules_h, jkk_schedule_trigger_cb_t trigger_cb, void *priv_data){
    if(schedules_h == NULL) {
        ESP_LOGE(TAG, "jkk_schedule_callback_all handle_list == NULL");
        return ESP_ERR_INVALID_ARG;
    }

    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    for (size_t handle_count = 0; handle_count < MAX_SCHEDULE_NUMBER; handle_count++) {
        if(handle_list[handle_count]){
            jkk_schedule_t *schedule = (jkk_schedule_t *)handle_list[handle_count];
            if(trigger_cb){
                schedule->trigger_cb = trigger_cb;
            }
            if(priv_data){
                schedule->priv_data = priv_data;
            }
        }
    }
    return ESP_OK;
}

esp_err_t jkk_schedule_callback_add(jkk_schedule_handle_t schedule_h, jkk_schedule_trigger_cb_t trigger_cb, void *priv_data){
    if(schedule_h == NULL) {
        ESP_LOGE(TAG, "jkk_schedule_callback_add handle_list == NULL");
        return ESP_ERR_INVALID_ARG;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)schedule_h;
    if(trigger_cb){
        schedule->trigger_cb = trigger_cb;
    }
    if(priv_data){
        schedule->priv_data = priv_data;
    }
    return ESP_OK;
}

esp_err_t jkk_schedule_clean(jkk_schedules_handle_t *schedules_h){
    if(schedules_h == NULL) {
        ESP_LOGE(TAG, "jkk_schedule_clean handle_list == NULL");
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = 0;
    time(&now);
    if(now < JKK_ERA) {
        ESP_LOGE(TAG, "jkk_schedule_clean No time");
        return ESP_ERR_INVALID_STATE;
    }

    /* Start/Delete the schedules */
    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    jkk_schedule_t *schedule = NULL;
    uint8_t schedule_count = 0;
    for (size_t handle_count = 0; handle_count < MAX_SCHEDULE_NUMBER; handle_count++) {
        /* Check for ONCE and expired schedules and delete them. */
        if(handle_list[handle_count]){
            schedule = (jkk_schedule_t *)handle_list[handle_count];
            if (jkk_schedule_is_expired(&schedule->trigger)) {
                /* This schedule has already expired. */
                ESP_LOGI(TAG, "Schedule %s does not repeat and has already expired. Deleting it.", schedule->name);
                jkk_schedule_delete(schedules_h, schedule);
                /* Removing the schedule from the list */
                handle_list[handle_count] = NULL;
                handle_count--;
                continue;
            }
            schedule_count++;
        }
    }
    return ESP_OK;
}

 jkk_schedule_t *jkk_schedule_find_missed(jkk_schedules_handle_t *schedules_h){
    if(schedules_h == NULL) {
        ESP_LOGE(TAG, "jkk_schedule_find_missed schedules_h == NULL");
        return NULL;
    }
    time_t now = 0;
    time(&now);

    struct tm current_time = {0};
    localtime_r(&now, &current_time);
    current_time.tm_sec -= (SECONDS_IN_DAY);
    mktime(&current_time);

    jkk_schedule_t *nearestSchedule = NULL;

    uint32_t nearestTime = 0;

    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    for (size_t handle_count = 0; handle_count < MAX_SCHEDULE_NUMBER; handle_count++) {
        if(handle_list[handle_count]){
            jkk_schedule_t *schedule = (jkk_schedule_t *)handle_list[handle_count];
            if(schedule->trigger.type == JKK_SCHEDULE_TYPE_RELATIVE || schedule->trigger.type == JKK_SCHEDULE_TYPE_INVALID) continue;

            uint32_t time_diff = jkk_schedule_get_next_schedule_time_diff(schedule->name, &schedule->trigger, &current_time, schedules_h->latitude_e5, schedules_h->longitude_e5);
            if(time_diff < SECONDS_IN_DAY){
                if(time_diff > nearestTime){
                    nearestTime = time_diff;
                    nearestSchedule = schedule;
                }
                ESP_LOGW(TAG, "jkk_schedule_find_missed test: %s, nearestTime: %ld", schedule->name, nearestTime);
            }
        }
    }
    if(nearestSchedule) ESP_LOGW(TAG, "jkk_schedule_find_missed found: %s, nearestTime: %ld", nearestSchedule->name, nearestTime);

    return nearestSchedule;
}

esp_err_t jkk_schedule_run_all(jkk_schedules_handle_t *schedules_h, bool runMissed){
    if(schedules_h == NULL) {
        ESP_LOGE(TAG, "jkk_schedule_run_all handle_list == NULL");
        return ESP_ERR_INVALID_ARG;
    }
    time_t now = 0;
    time(&now);
    if(now < JKK_ERA) {
        ESP_LOGE(TAG, "jkk_schedule_run_all No time");
        return ESP_ERR_INVALID_STATE;
    }

    if(runMissed){
        jkk_schedule_t *nearestSchedule = jkk_schedule_find_missed(schedules_h);
        if(nearestSchedule){
            if (nearestSchedule->trigger_cb) {
                nearestSchedule->trigger_cb((jkk_schedule_handle_t)nearestSchedule, nearestSchedule->priv_data);
            }
        }
    }

    /* Create the single master FreeRTOS timer for this schedules handle if not yet done */
    if (schedules_h->timer == NULL) {
        schedules_h->timer = xTimerCreate("jkk_sched_m", pdMS_TO_TICKS(1000),
                                          pdFALSE, (void *)schedules_h,
                                          jkk_schedule_master_timer_cb);
    }

    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;
    for (size_t handle_count = 0; handle_count < MAX_SCHEDULE_NUMBER; handle_count++) {
        if(handle_list[handle_count]){
            jkk_schedule_t *schedule = (jkk_schedule_t *)handle_list[handle_count];
            schedule->parent = schedules_h;

            time_t now = 0;
            time(&now);
            struct tm current_time = {0};
            localtime_r(&now, &current_time);

            if(now > JKK_ERA) {
                schedule->next_scheduled_time_diff = jkk_schedule_get_next_schedule_time_diff(schedule->name, &schedule->trigger, &current_time, schedules_h->latitude_e5, schedules_h->longitude_e5);
            }

            jkk_schedule_create_timer(schedule);
            jkk_schedule_start_timer(schedule);
        }
    }
    return ESP_OK;
}

