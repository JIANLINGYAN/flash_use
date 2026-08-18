/**
 * fastflash_sim_port.h - fast_flashdb_table 移植层头文件（注册式，平台无关）
 *
 * 声明把 fast_flashdb_table 的 flash_ops_t 桥接到统一 flash_hal_t 的
 * 注册接口：目标平台实现 flash_hal_t 后调用 fast_flash_port_init 注册。
 * 与 easyflash/ef_port.h、flashdb/fal_flash_sim_port.h 同一定位。
 */
#ifndef FASTFLASH_SIM_PORT_H
#define FASTFLASH_SIM_PORT_H

#include <stdint.h>
#include "flash_hal.h"
#include "fast_flash_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册 HAL（内部用 hal 实现组件所需的 flash_ops_t）。
 * @param hal  统一 flash_hal_t（实现 read/write/erase）
 * @return     0 成功，非 0 失败
 */
int fast_flash_port_init(const flash_hal_t *hal);

/**
 * 供组件使用的 flash_ops_t 实例（由 fast_flash_port_init 桥接到 hal）。
 */
extern const flash_ops_t sim_flash_ops;

#ifdef __cplusplus
}
#endif

#endif /* FASTFLASH_SIM_PORT_H */
