/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/device.h 最小模拟。
 * 组件仅把 device 指针透传给 flash 设备 API，不解析内部字段。
 * sim 指向统一 flash_hal_t 注册实例；params 指向设备参数（见 drivers/flash.h）。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_DEVICE_H_
#define ZEPHYR_INCLUDE_ZEPHYR_DEVICE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct flash_parameters;

/**
 * 模拟设备。本平台中即"一块 Flash 介质"的包装：
 *  - sim     : flash_hal_t*（统一 HAL 注册实例）
 *  - params  : flash_parameters*（write_block_size / erase_value）
 *  - page_size: 介质页大小（模拟层取 erase_size）
 */
struct device {
	void *sim;
	const struct flash_parameters *params;
	size_t page_size;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_DEVICE_H_ */
