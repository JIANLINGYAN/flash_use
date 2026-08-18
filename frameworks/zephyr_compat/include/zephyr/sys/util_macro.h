/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/sys/util_macro.h 最小模拟。
 * 提供 BIT / BIT64 / IS_POWER_OF_TWO / IS_ENABLED / FIELD_GET / LSB_GET。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_MACRO_H_
#define ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_MACRO_H_

#include <zephyr/sys/util_internal.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#ifndef BIT64
#define BIT64(_n) (1ULL << (_n))
#endif

#ifndef BIT_MASK
#define BIT_MASK(n) (BIT(n) - 1UL)
#endif

#define IS_POWER_OF_TWO(x) (((x) != 0U) && (((x) & ((x) - 1U)) == 0U))

#define LSB_GET(value) ((value) & -(value))

#define FIELD_GET(mask, value) (((value) & (mask)) / LSB_GET(mask))

#define FIELD_PREP(mask, value) (((value) * LSB_GET(mask)) & (mask))

#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_MACRO_H_ */
