#ifndef FAST_FLASH_LOG_H
#define FAST_FLASH_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// 日志级别
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
} log_level_t;

// 平台相关的日志函数
void flash_log_set_level(log_level_t level);
void flash_log_print(log_level_t level, const char *format, ...);

// 简化的宏定义
#define TRACE_ERROR(...) flash_log_print(LOG_LEVEL_ERROR, "[ERROR] " __VA_ARGS__)
#define TRACE_WARN(...)  flash_log_print(LOG_LEVEL_WARN,  "[WARN]  " __VA_ARGS__)
#define TRACE_INFO(...)   flash_log_print(LOG_LEVEL_INFO,  "[INFO]  " __VA_ARGS__)
#define TRACE_DEBUG(...)  flash_log_print(LOG_LEVEL_DEBUG, "[DEBUG] " __VA_ARGS__)

// 默认使用INFO级别
#define TRACE(...)        TRACE_INFO(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // FAST_FLASH_LOG_H