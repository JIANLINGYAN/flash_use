#include "fast_flash_log.h"
#include <stdarg.h>

static log_level_t current_log_level = LOG_LEVEL_INFO;

void flash_log_set_level(log_level_t level) {
    current_log_level = level;
}

void flash_log_print(log_level_t level, const char *format, ...) {
    if (level > current_log_level) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // 根据级别选择输出流
    FILE *output = (level <= LOG_LEVEL_WARN) ? stderr : stdout;
    
    vfprintf(output, format, args);
    fflush(output);
    
    va_end(args);
}