/**
 * yaffs_sim_port.h - YAFFS 移植层头文件（注册式，平台无关）
 *
 * 声明把 YAFFS Direct 桥接到统一 flash_hal_t 的注册接口：目标平台实现
 * flash_hal_t 后调用 yaffs_port_init 注册，再 yaffs_sim_start_up 启动。
 * 与 easyflash/ef_port.h、flashdb/fal_flash_sim_port.h 同一定位。
 */
#ifndef YAFFS_SIM_PORT_H
#define YAFFS_SIM_PORT_H

#include <stdint.h>
#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 模拟 NAND 几何（2K page + 32B spare、32 页/块） */
#define YAFFS_SIM_CHUNK_BYTES     2048u
#define YAFFS_SIM_SPARE_BYTES     32u
#define YAFFS_SIM_CHUNKS_PER_BLOCK 32u
#define YAFFS_SIM_BLOCKS          8u

/**
 * 注册 HAL（几何从 hal 读取：擦除块须等于
 * chunks_per_block*(chunk+spare)）。
 * @param hal 统一 flash_hal_t（实现 read/write/erase）
 * @return    0 成功；-2 几何不匹配
 */
int yaffs_port_init(const flash_hal_t *hal);

/**
 * 启动 YAFFS（注册设备）。返回 0 成功。
 */
int yaffs_sim_start_up(void);

#ifdef __cplusplus
}
#endif

#endif /* YAFFS_SIM_PORT_H */
