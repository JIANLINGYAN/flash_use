/**
 * tym_setting_log.h - TYM Setting 框架日志抽象（去耦补充）
 *
 * 原版依赖 Airoha hal_log.h 的 log_hal_info。本头把框架内日志收口为
 * LOG_E/W/I 三个宏，移植层映射到目标平台日志（本平台输出 stdout）。
 */

#ifndef TYM_SETTING_LOG_H
#define TYM_SETTING_LOG_H

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* uint32 在 64 位主机是 unsigned long，格式化统一用 PRIu32/PRIx32 */
#define LOG_E(...)  do { printf("[TYM][E] " __VA_ARGS__); printf("\n"); } while (0)
#define LOG_W(...)  do { printf("[TYM][W] " __VA_ARGS__); printf("\n"); } while (0)
#define LOG_I(...)  do { printf("[TYM][I] " __VA_ARGS__); printf("\n"); } while (0)

#define TYM_FMT_U  PRIu32
#define TYM_FMT_X  PRIx32

#endif /* TYM_SETTING_LOG_H */
