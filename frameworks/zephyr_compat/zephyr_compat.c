/**
 * zephyr_compat.c - Zephyr 兼容层实现
 *
 * 桥接映射：
 *   - flash_read/write     -> hal->read/write（地址 = 介质绝对偏移）
 *   - flash_erase          -> hal->erase（按块）
 *   - flash_flatten        -> hal->erase（本平台介质均需显式擦除）
 *   - flash_area_*         -> 基于注册分区偏移的 hal 操作
 *   - crc8_ccitt/crc32_ieee-> 纯软件实现（poly 0x07 / 0xEDB88320）
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include "zephyr_compat.h"

/* ==================== 注册状态 ==================== */

static struct {
	const flash_hal_t *hal;
	uint32_t erase_size;
	uint8_t erase_value;
	struct flash_parameters params;
	struct device device;
	struct flash_area area;
	int area_valid;
} s_reg;

/* ==================== 注册接口 ==================== */

const struct device *zephyr_compat_register_flash(const flash_hal_t *hal,
						  uint32_t erase_size,
						  uint32_t write_block_size,
						  uint8_t erase_value)
{
	s_reg.hal = hal;
	s_reg.erase_size = erase_size;
	s_reg.erase_value = erase_value;
	s_reg.params.write_block_size = write_block_size;
	s_reg.params.erase_value = erase_value;
	s_reg.device.sim = (void *)hal;
	s_reg.device.params = &s_reg.params;
	s_reg.device.page_size = erase_size;
	return &s_reg.device;
}

int zephyr_compat_register_area(uint8_t id, const struct device *zdev,
				off_t off, size_t size)
{
	if (zdev == NULL) {
		return -1;
	}
	s_reg.area.fa_id = id;
	s_reg.area.fa_off = off;
	s_reg.area.fa_size = size;
	s_reg.area.fa_dev = zdev;
	s_reg.area_valid = 1;
	return 0;
}

/* ==================== flash 设备 API ==================== */

const struct flash_parameters *flash_get_parameters(const struct device *dev)
{
	if (dev == NULL || dev->params == NULL) {
		return NULL;
	}
	return dev->params;
}

size_t flash_get_write_block_size(const struct device *dev)
{
	const struct flash_parameters *p = flash_get_parameters(dev);

	if (p == NULL) {
		return 0;
	}
	return p->write_block_size;
}

int flash_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	const flash_hal_t *hal = (dev != NULL) ? (const flash_hal_t *)dev->sim : NULL;
	if (hal == NULL || data == NULL) {
		return -EINVAL;
	}
	if (hal->read(hal->ctx, (uint32_t)offset, data, (uint32_t)len) != 0) {
		return -EIO;
	}
	return 0;
}

int flash_write(const struct device *dev, off_t offset, const void *data,
		size_t len)
{
	const flash_hal_t *hal = (dev != NULL) ? (const flash_hal_t *)dev->sim : NULL;
	if (hal == NULL || data == NULL) {
		return -EINVAL;
	}
	if (hal->write(hal->ctx, (uint32_t)offset, data, (uint32_t)len) != 0) {
		return -EIO;
	}
	return 0;
}

int flash_erase(const struct device *dev, off_t offset, size_t size)
{
	const flash_hal_t *hal = (dev != NULL) ? (const flash_hal_t *)dev->sim : NULL;
	if (hal == NULL) {
		return -EINVAL;
	}
	if (size == 0) {
		return 0;
	}
	if (hal->erase(hal->ctx, (uint32_t)offset, (uint32_t)size) != 0) {
		return -EIO;
	}
	return 0;
}

int flash_flatten(const struct device *dev, off_t offset, size_t size)
{
	/* 本平台 NOR/NAND 均需显式擦除，flatten 即 erase */
	return flash_erase(dev, offset, size);
}

int flash_get_page_info_by_offs(const struct device *dev, off_t offset,
				struct flash_pages_info *info)
{
	const struct device *d = (dev != NULL) ? dev : &s_reg.device;

	if (info == NULL || d->page_size == 0) {
		return -EINVAL;
	}
	info->size = d->page_size;
	info->start_offset = (offset / (off_t)d->page_size) * (off_t)d->page_size;
	info->index = (uint32_t)(offset / (off_t)d->page_size);
	return 0;
}

/* ==================== flash_area 分区 API ==================== */

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
	if (fa == NULL) {
		return -EINVAL;
	}
	if (!s_reg.area_valid || s_reg.area.fa_id != id) {
		*fa = NULL;
		return -ENOENT;
	}
	*fa = &s_reg.area;
	return 0;
}

void flash_area_close(const struct flash_area *fa)
{
	(void)fa;
}

int flash_area_read(const struct flash_area *fa, off_t off, void *dst,
		    size_t len)
{
	if (fa == NULL || fa->fa_dev == NULL || dst == NULL) {
		return -EINVAL;
	}
	if (off + (off_t)len > (off_t)fa->fa_size) {
		return -EINVAL;
	}
	return flash_read(fa->fa_dev, fa->fa_off + off, dst, len);
}

int flash_area_write(const struct flash_area *fa, off_t off, const void *src,
		     size_t len)
{
	if (fa == NULL || fa->fa_dev == NULL || src == NULL) {
		return -EINVAL;
	}
	if (off + (off_t)len > (off_t)fa->fa_size) {
		return -EINVAL;
	}
	return flash_write(fa->fa_dev, fa->fa_off + off, src, len);
}

int flash_area_erase(const struct flash_area *fa, off_t off, size_t len)
{
	if (fa == NULL || fa->fa_dev == NULL) {
		return -EINVAL;
	}
	if (off + (off_t)len > (off_t)fa->fa_size) {
		return -EINVAL;
	}
	return flash_erase(fa->fa_dev, fa->fa_off + off, len);
}

int flash_area_flatten(const struct flash_area *fa, off_t off, size_t len)
{
	return flash_area_erase(fa, off, len);
}

uint32_t flash_area_align(const struct flash_area *fa)
{
	const struct flash_parameters *p;

	if (fa == NULL || fa->fa_dev == NULL) {
		return 1;
	}
	p = flash_get_parameters(fa->fa_dev);
	return (p != NULL) ? (uint32_t)p->write_block_size : 1;
}

int flash_area_get_sectors(int fa_id, uint32_t *count,
			   struct flash_sector *sectors)
{
	uint32_t n, i;
	off_t off;
	size_t sz;

	if (count == NULL || !s_reg.area_valid || s_reg.area.fa_id != fa_id ||
	    s_reg.erase_size == 0) {
		return -EINVAL;
	}
	sz = s_reg.erase_size;
	n = (uint32_t)(s_reg.area.fa_size / sz);
	if (sectors == NULL) {
		*count = n;
		return 0;
	}
	if (*count < n) {
		return -ENOMEM;
	}
	for (i = 0, off = s_reg.area.fa_off; i < n; i++, off += (off_t)sz) {
		sectors[i].fs_off = off;
		sectors[i].fs_size = sz;
	}
	*count = n;
	return 0;
}

uint8_t flash_area_erased_val(const struct flash_area *fa)
{
	if (fa == NULL || fa->fa_dev == NULL) {
		return 0xFF;
	}
	return s_reg.erase_value;
}

/* ==================== CRC（纯软件） ==================== */

uint8_t crc8_ccitt(uint8_t initial_value, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	uint8_t crc = initial_value;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= p[i];
		for (bit = 0; bit < 8; bit++) {
			if (crc & 0x80U) {
				crc = (uint8_t)((crc << 1) ^ 0x07U);
			} else {
				crc = (uint8_t)(crc << 1);
			}
		}
	}
	return crc;
}

uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	return crc32_ieee_update(0x0, data, len);
}

uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len)
{
	/* 源自 Zephyr crc32_sw.c：表由多项式 0xEDB88320 生成（半字节查表） */
	static const uint32_t table[16] = {
		0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
		0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
		0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
		0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
	};
	size_t i;

	crc = ~crc;
	for (i = 0; i < len; i++) {
		uint8_t byte = data[i];

		crc = (crc >> 4) ^ table[(crc ^ byte) & 0x0f];
		crc = (crc >> 4) ^ table[(crc ^ ((uint32_t)byte >> 4)) & 0x0f];
	}
	return ~crc;
}
