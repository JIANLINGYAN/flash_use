/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/sys/__assert.h 最小模拟。
 * __ASSERT_NO_MSG 编译期空实现（仿真环境不触发断言）。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_SYS___ASSERT_H_
#define ZEPHYR_INCLUDE_ZEPHYR_SYS___ASSERT_H_

#include <assert.h>

#define __ASSERT_NO_MSG(test) ((void)0)

#endif /* ZEPHYR_INCLUDE_ZEPHYR_SYS___ASSERT_H_ */
