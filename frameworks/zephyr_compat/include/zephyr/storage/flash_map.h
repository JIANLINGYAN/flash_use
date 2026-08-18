/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/storage/flash_map.h 最小模拟。
 * 声明 flash_area 分区抽象（FCB 依赖），实现见 zephyr_compat.c。
 * 本平台将单个模拟设备映射为一个分区（fa_id=0）。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_STORAGE_FLASH_MAP_H_
#define ZEPHYR_INCLUDE_ZEPHYR_STORAGE_FLASH_MAP_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Flash 分区 */
struct flash_area {
	/** 分区 ID */
	uint8_t fa_id;
	uint16_t pad16;
	/** 分区起始偏移（相对介质） */
	off_t fa_off;
	/** 分区大小 */
	size_t fa_size;
	/** 背板设备 */
	const struct device *fa_dev;
};

/** 扇区边界描述 */
struct flash_sector {
	/** 扇区起始偏移（相对介质） */
	off_t fs_off;
	/** 扇区大小 */
	size_t fs_size;
};

/** 打开分区（id 由注册表提供；未注册返回 -ENOENT） */
int flash_area_open(uint8_t id, const struct flash_area **fa);

/** 关闭分区（NOP） */
void flash_area_close(const struct flash_area *fa);

/** 读 */
int flash_area_read(const struct flash_area *fa, off_t off, void *dst,
		    size_t len);

/** 写 */
int flash_area_write(const struct flash_area *fa, off_t off, const void *src,
		     size_t len);

/** 擦除 */
int flash_area_erase(const struct flash_area *fa, off_t off, size_t len);

/** 擦除或填充擦除值 */
int flash_area_flatten(const struct flash_area *fa, off_t off, size_t len);

/** 写对齐（字节） */
uint32_t flash_area_align(const struct flash_area *fa);

/** 枚举分区内扇区 */
int flash_area_get_sectors(int fa_id, uint32_t *count,
			   struct flash_sector *sectors);

/** 擦除值（NOR 通常 0xFF） */
uint8_t flash_area_erased_val(const struct flash_area *fa);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_STORAGE_FLASH_MAP_H_ */
