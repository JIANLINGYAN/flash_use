/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/drivers/flash.h 最小模拟。
 * 声明 Flash 设备 API（flash_read/write/erase/flatten/get_parameters 等），
 * 实现见 zephyr_compat.c（桥接 simulator/flash_sim.c）。
 * 模拟设备为"需显式擦除"的 NOR/NAND，故 flash_params_get_erase_cap()
 * 返回 FLASH_ERASE_C_EXPLICIT（保持与 ZMS 擦除分支一致）。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_DRIVERS_FLASH_H_
#define ZEPHYR_INCLUDE_ZEPHYR_DRIVERS_FLASH_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 本兼容层模拟"需显式擦除"的 NOR/NAND 介质 */
#ifndef CONFIG_FLASH_HAS_EXPLICIT_ERASE
#define CONFIG_FLASH_HAS_EXPLICIT_ERASE
#endif

/** Flash 介质参数（write_block_size 与 erase_value 由模拟设备提供） */
struct flash_parameters {
	/** 最小写对齐与写单位（兼容层可运行时赋值，vendor 只读） */
	size_t write_block_size;
	/** 内部隐藏能力位 */
	struct {
		/** 设备写前无需显式擦除 */
		bool no_explicit_erase : 1;
	} caps;
	/** 擦除后介质填充值（NOR 通常 0xFF） */
	uint8_t erase_value;
};

/** 需显式擦除的能力位 */
#define FLASH_ERASE_C_EXPLICIT 0x01

static inline int flash_params_get_erase_cap(const struct flash_parameters *p)
{
	ARG_UNUSED(p);
#if defined(CONFIG_FLASH_HAS_EXPLICIT_ERASE)
	return FLASH_ERASE_C_EXPLICIT;
#else
	return 0;
#endif
}

/** 页信息（本平台取 erase_size 作为页/扇区大小） */
struct flash_pages_info {
	off_t start_offset; /**< 页起始偏移 */
	size_t size;        /**< 页大小 */
	uint32_t index;     /**< 页序号 */
};

/** 读取 Flash 设备参数（NULL 表示失败） */
const struct flash_parameters *flash_get_parameters(const struct device *dev);

/** 读取最小写块大小 */
size_t flash_get_write_block_size(const struct device *dev);

/** 读取 */
int flash_read(const struct device *dev, off_t offset, void *data, size_t len);

/** 写入 */
int flash_write(const struct device *dev, off_t offset, const void *data,
		size_t len);

/** 显式擦除（按块对齐） */
int flash_erase(const struct device *dev, off_t offset, size_t size);

/** 擦除或填充为擦除值（无需显式擦除的设备亦可用） */
int flash_flatten(const struct device *dev, off_t offset, size_t size);

/** 查询 offset 所在页信息 */
int flash_get_page_info_by_offs(const struct device *dev, off_t offset,
				struct flash_pages_info *info);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_DRIVERS_FLASH_H_ */
