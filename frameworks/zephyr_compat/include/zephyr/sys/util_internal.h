/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/sys/util_internal.h 最小模拟。
 * 仅包含 IS_ENABLED() 展开所需的内部宏（源自 Zephyr 原版实现）。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_INTERNAL_H_
#define ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_INTERNAL_H_

#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)

#define _XXXX1 _YYYY,

#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)

#define Z_IS_ENABLED3(ignore_this, val, ...) val

#endif /* ZEPHYR_INCLUDE_ZEPHYR_SYS_UTIL_INTERNAL_H_ */
