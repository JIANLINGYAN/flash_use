/**
 * littlefs_sim_port.h - LittleFS 移植层头文件（注册式，平台无关）
 *
 * 声明把 LittleFS 桥接到统一 flash_hal_t 的注册接口：目标平台实现
 * flash_hal_t（read/write/erase + 几何参数）后调用 littlefs_port_init，
 * 即可获得填充好的 lfs_config 并挂载使用。与 easyflash/ef_port.h、
 * flashdb/fal_flash_sim_port.h 同一定位。
 */
#ifndef LITTLEFS_SIM_PORT_H
#define LITTLEFS_SIM_PORT_H

#include <stdint.h>
#include "flash_hal.h"
#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册 HAL 并填充 lfs_config。
 * @param hal  统一 flash_hal_t（实现 read/write/erase；几何取
 *             hal->erase_size/总容量等）
 * @param base LittleFS 在介质上的起始偏移（建议块对齐）
 * @param cfg  输出 lfs_config（read/prog/erase/sync 回调 + 几何参数）
 * @return     0 成功，非 0 失败
 */
int littlefs_port_init(const flash_hal_t *hal, uint32_t base,
                       struct lfs_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* LITTLEFS_SIM_PORT_H */
