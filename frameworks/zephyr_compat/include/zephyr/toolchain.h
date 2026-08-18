/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/toolchain.h 最小模拟。
 * 提供 __packed / __unused / ALWAYS_INLINE / ARG_UNUSED / BUILD_ASSERT。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_TOOLCHAIN_H_
#define ZEPHYR_INCLUDE_ZEPHYR_TOOLCHAIN_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

/* 编译期静态断言（C11 _Static_assert） */
#define BUILD_ASSERT(EXPR, MSG...) _Static_assert((EXPR), "" MSG)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_TOOLCHAIN_H_ */
