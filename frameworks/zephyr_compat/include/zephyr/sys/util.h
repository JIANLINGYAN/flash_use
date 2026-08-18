/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/sys/util.h 最小模拟。
 * 提供 KB / SIZEOF_FIELD / GENMASK / GENMASK64 / ZTESTABLE_STATIC。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_H_
#define ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KB(x) (((size_t)(x)) << 10)

#define SIZEOF_FIELD(type, member) sizeof((((type *)0)->member))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define BITS_PER_LONG (sizeof(long) * 8)

#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#define GENMASK64(h, l) \
	(((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (63 - (h))))

/* 非 ZTEST 环境：可测试的 static 函数退化为普通 static */
#define ZTESTABLE_STATIC static

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_H_ */
