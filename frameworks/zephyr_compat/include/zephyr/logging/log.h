/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/logging/log.h 最小模拟。
 * 组件内 LOG_* 输出到 stdout（[ZVLOG] 前缀），便于调试与回归观察。
 * LOG_MODULE_REGISTER 为空（无模块注册概念）。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_ZEPHYR_LOGGING_LOG_H_

#include <stdio.h>

#define LOG_MODULE_REGISTER(module, level)

#define LOG_ERR(...)   do { printf("[ZVLOG][E] " __VA_ARGS__); printf("\n"); } while (0)
#define LOG_WRN(...)   do { printf("[ZVLOG][W] " __VA_ARGS__); printf("\n"); } while (0)
#define LOG_INF(...)   do { printf("[ZVLOG][I] " __VA_ARGS__); printf("\n"); } while (0)
/* 调试日志：置空，避免刷屏且规避 64 位 %llx 格式差异 */
#define LOG_DBG(...)   do { } while (0)

#define LOG_MODULE_DECLARE(module, level)

#endif /* ZEPHYR_INCLUDE_ZEPHYR_LOGGING_LOG_H_ */
