/**
 * spiffs_sim_port.h - SPIFFS 移植层头文件（注册式，平台无关）
 *
 * 声明把 SPIFFS 桥接到统一 flash_hal_t 的注册接口：目标平台实现
 * flash_hal_t 后调用 spiffs_port_init 注册，即可挂载使用。与
 * easyflash/ef_port.h、flashdb/fal_flash_sim_port.h 同一定位。
 */
#ifndef SPIFFS_SIM_PORT_H
#define SPIFFS_SIM_PORT_H

#include <stdint.h>
#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册 HAL（几何从 hal->total_size / hal->erase_size 读取）。
 * @param hal 统一 flash_hal_t（实现 read/write/erase）
 * @return    0 成功，非 0 失败
 */
int spiffs_port_init(const flash_hal_t *hal);

/**
 * 挂载 SPIFFS。返回 SPIFFS_OK 或错误码（全新介质会返回未格式化错误）。
 */
int spiffs_sim_mount(void);

/**
 * 格式化 SPIFFS（需已通过 spiffs_port_init 注册）。
 */
int spiffs_sim_format(void);

/**
 * 卸载 SPIFFS。
 */
void spiffs_sim_unmount(void);

/**
 * 返回内部 spiffs 实例（供测试程序操作文件）。
 */
void *spiffs_sim_fs(void);

#ifdef __cplusplus
}
#endif

#endif /* SPIFFS_SIM_PORT_H */
