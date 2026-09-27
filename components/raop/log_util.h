/*
 * Logging shim for the RAOP code: the same LOG_* macros it was written against, routed to
 * ESP-IDF's logger. Replaces the original GPL log_util.h, so this component stays MIT.
 * (c) 2026 Dmytro Skorin, MIT License.
 */
#pragma once

#include "esp_log.h"
#include "platform.h"

typedef enum { lERROR = 0, lWARN, lINFO, lDEBUG, lSDEBUG } log_level;

extern log_level raop_loglevel;
extern log_level util_loglevel;

#define LOG_ERROR(fmt, ...) ESP_LOGE("raop", "%s:%d " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  do { if (*loglevel >= lWARN)  ESP_LOGW("raop", "%s:%d " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmt, ...)  do { if (*loglevel >= lINFO)  ESP_LOGI("raop", "%s:%d " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define LOG_DEBUG(fmt, ...) do { if (*loglevel >= lDEBUG) ESP_LOGD("raop", "%s:%d " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define LOG_SDEBUG(fmt, ...) do { if (*loglevel >= lSDEBUG) ESP_LOGV("raop", "%s:%d " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
