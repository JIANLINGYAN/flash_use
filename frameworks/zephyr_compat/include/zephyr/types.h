/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/types.h 最小模拟。
 * 仅提供 Zephyr 存储组件（FCB/NVS/ZMS）编译所需的基本类型。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_TYPES_H_
#define ZEPHYR_INCLUDE_ZEPHYR_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;
typedef int8_t   s8_t;
typedef int16_t  s16_t;
typedef int32_t  s32_t;
typedef int64_t  s64_t;

typedef uintptr_t uintptr_t;

#endif /* ZEPHYR_INCLUDE_ZEPHYR_TYPES_H_ */
