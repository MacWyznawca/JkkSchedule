/* JkkSchedule - Lightweight scheduling library for ESP-IDF
 * Copyright (C) 2025 Jaromir Kopp (JKK)
 * NVS persistence helpers for schedule storage and recovery
 *
 * Inspired by the original Espressif esp_schedule component and significantly
 * redesigned with multiple fixes and behavior improvements.
 */

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <esp_log.h>
#include <nvs.h>
#include "JkkScheduleInternal.h"

static const char *TAG = "jkk_schedule_nvs";

esp_err_t jkk_schedule_nvs_add(jkk_schedules_handle_t *schedules_h, jkk_schedule_t *schedule){
    if (!schedules_h->nvs_enabled) {
        ESP_LOGD(TAG, "NVS not enabled. Not adding to NVS.");
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(schedules_h->schedule_nvs_partition, schedules_h->schedule_nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return err;
    }

    err = nvs_set_blob(nvs_handle, schedule->name, schedule, sizeof(jkk_schedule_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set failed with error %d", err);
        nvs_close(nvs_handle);
        return err;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Schedule %s added in NVS", schedule->name);
    return ESP_OK;
}

esp_err_t jkk_schedule_nvs_remove_all(jkk_schedules_handle_t *schedules_h){
    if (!schedules_h->nvs_enabled) {
        ESP_LOGD(TAG, "NVS not enabled. Not removing from NVS.");
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(schedules_h->schedule_nvs_partition, schedules_h->schedule_nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return err;
    }
    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase all keys failed with error %d", err);
        nvs_close(nvs_handle);
        return err;
    }
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "All schedules removed from NVS");
    return ESP_OK;
}

esp_err_t jkk_schedule_nvs_remove(jkk_schedules_handle_t *schedules_h, jkk_schedule_t *schedule){
    if (!schedules_h->nvs_enabled) {
        ESP_LOGD(TAG, "NVS not enabled. Not removing from NVS.");
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(schedules_h->schedule_nvs_partition, schedules_h->schedule_nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return err;
    }
    err = nvs_erase_key(nvs_handle, schedule->name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase key failed with error %d", err);
        nvs_close(nvs_handle);
        return err;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Schedule %s removed from NVS", schedule->name);
    return ESP_OK;
}

static jkk_schedule_handle_t jkk_schedule_nvs_get(jkk_schedules_handle_t *schedules_h, char *nvs_key){
    if (!schedules_h->nvs_enabled) {
        ESP_LOGD(TAG, "NVS not enabled. Not getting from NVS.");
        return NULL;
    }
    size_t buf_size;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open_from_partition(schedules_h->schedule_nvs_partition, schedules_h->schedule_nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return NULL;
    }
    err = nvs_get_blob(nvs_handle, nvs_key, NULL, &buf_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS get failed with error %d", err);
        nvs_close(nvs_handle);
        return NULL;
    }
    jkk_schedule_t *schedule = (jkk_schedule_t *)calloc(1, buf_size);
    if (schedule == NULL) {
        ESP_LOGE(TAG, "Could not allocate handle");
        nvs_close(nvs_handle);
        return NULL;
    }
    err = nvs_get_blob(nvs_handle, nvs_key, schedule, &buf_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS get failed with error %d", err);
        nvs_close(nvs_handle);
        free(schedule);
        return NULL;
    }
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "jkk_schedule_nvs_get Schedule %s found in NVS", schedule->name);
    return (jkk_schedule_handle_t) schedule;
}

int8_t jkk_schedule_nvs_get_all(jkk_schedules_handle_t *schedules_h){
    if (!schedules_h->nvs_enabled) {
        ESP_LOGD(TAG, "NVS not enabled. Not Initialising NVS.");
        return INT8_MIN;
    }

    jkk_schedule_handle_t *handle_list = schedules_h->handle_list;

    if (handle_list == NULL) {
        ESP_LOGE(TAG, "Could not allocate schedule list");
        return INT8_MIN;
    }

    int8_t schedule_count = 0;

    nvs_entry_info_t nvs_entry;

    nvs_iterator_t nvs_iterator = NULL;
    esp_err_t err = nvs_entry_find(schedules_h->schedule_nvs_partition, schedules_h->schedule_nvs_namespace, NVS_TYPE_BLOB, &nvs_iterator);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No entry found in NVS");
        return 0;
    }
    while (err == ESP_OK) {
        nvs_entry_info(nvs_iterator, &nvs_entry);
        ESP_LOGI(TAG, "Found schedule in NVS with key: %s", nvs_entry.key);
        handle_list[schedule_count] = jkk_schedule_nvs_get(schedules_h, nvs_entry.key);
        if (handle_list[schedule_count] != NULL) {
            /* Increase count only if nvs_get was successful */
            schedule_count++;
            if (schedule_count >= MAX_SCHEDULE_NUMBER) {
                ESP_LOGI(TAG, "To many entries, make MAX_SCHEDULE_NUMBER bigger");
                nvs_release_iterator(nvs_iterator);
                return schedule_count * -1;
            }
        }
        err = nvs_entry_next(&nvs_iterator);
    }
    nvs_release_iterator(nvs_iterator);

    ESP_LOGI(TAG, "Found %d schedules in NVS", schedule_count);
    return schedule_count;
}

bool jkk_schedule_nvs_is_enabled(jkk_schedules_handle_t *schedules_h){
    return schedules_h->nvs_enabled;
}
