/**
 * fatfs_sim_port.h - FatFs 移植层头文件（注册式，平台无关）
 *
 * 声明把 FatFs 桥接到统一 flash_hal_t 的注册接口：目标平台实现
 * flash_hal_t 后调用 fatfs_port_init 注册，即可挂载使用。与
 * easyflash/ef_port.h、flashdb/fal_flash_sim_port.h 同一定位。
 */
#ifndef FATFS_SIM_PORT_H
#define FATFS_SIM_PORT_H

#include <stdint.h>
#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册 HAL 并配置 FatFs 磁盘底层。
 * @param hal       统一 flash_hal_t（实现 read/write/erase）
 * @param disk_base 磁盘在介质上的起始偏移（建议块对齐，默认 0）
 * @return          0 成功，非 0 失败
 */
int fatfs_port_init(const flash_hal_t *hal, uint32_t disk_base);

/**
 * 返回介质扇区大小（FatFs 以扇区为基本读写单位）。
 */
uint32_t fatfs_sim_sector_size(void);

#ifdef __cplusplus
}
#endif

#endif /* FATFS_SIM_PORT_H */
