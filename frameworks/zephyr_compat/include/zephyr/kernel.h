/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/kernel.h 最小模拟。
 * 仅提供组件使用的 k_mutex（单线程仿真：空实现）与 K_FOREVER。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_KERNEL_H_
#define ZEPHYR_INCLUDE_ZEPHYR_KERNEL_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 互斥锁（单线程仿真：占位即可） */
struct k_mutex {
	uint8_t _reserved;
};

#define K_FOREVER ((int64_t)-1)

static inline void k_mutex_init(struct k_mutex *m)
{
	(void)m;
}

static inline int k_mutex_lock(struct k_mutex *m, int64_t timeout)
{
	(void)m;
	(void)timeout;
	return 0;
}

static inline int k_mutex_unlock(struct k_mutex *m)
{
	(void)m;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_KERNEL_H_ */
