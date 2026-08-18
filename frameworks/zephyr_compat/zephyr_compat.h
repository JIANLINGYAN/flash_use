/**
 * zephyr_compat.h - Zephyr 兼容层移植接口
 *
 * 定位：FCB/NVS/ZMS 三组件 vendor 零修改，通过本兼容层把 Zephyr 的
 * flash 设备 API / flash_area 分区 API / k_mutex / CRC 桥接到
 * simulator/flash_sim.c。
 *
 * 使用方式（在组件 sim_port 或测试程序中）：
 *   1. zephyr_compat_register_flash(dev, erase_size, write_block_size, 0xFF)
 *      注册一块模拟介质为 Zephyr 设备（返回 struct device*）；
 *   2. zephyr_compat_register_area(0, dev, 0, capacity)
 *      注册分区（FCB 需要；NVS/ZMS 直接用 device）。
 * 之后即可调用 nvs_mount / zms_mount / fcb_init。
 */

#ifndef ZEPHYR_COMPAT_H
#define ZEPHYR_COMPAT_H

#include <stdint.h>
#include <sys/types.h>

#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册模拟介质为 Zephyr flash 设备。
 * @param dev              flash_sim 设备句柄
 * @param erase_size       介质擦除块大小（同时作为页/扇区大小）
 * @param write_block_size 最小写单位（NOR 为 1）
 * @param erase_value      擦除值（NOR 为 0xFF）
 * @return Zephyr struct device*（单例，可重复注册覆盖）
 */
const struct device *zephyr_compat_register_flash(flash_dev_t *dev,
						  uint32_t erase_size,
						  uint32_t write_block_size,
						  uint8_t erase_value);

/**
 * 注册分区（供 flash_area_open 使用，单分区）。
 * @param id     分区 ID（通常 0）
 * @param zdev   zephyr_compat_register_flash 返回的设备
 * @param off    分区起始偏移（相对介质）
 * @param size   分区大小
 * @return 0 成功；-1 参数非法
 */
int zephyr_compat_register_area(uint8_t id, const struct device *zdev,
				off_t off, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_H */
