/**
 * fastflash_sim_port.h - fast_flashdb_table 模拟基座移植层头文件
 *
 * 声明对接本平台模拟基座（simulator/flash_sim.c）的移植接口，供组件
 * 与测试程序引用。与 easyflash/ef_port.h、flashdb 的 fal 头文件同一定位。
 */
#ifndef FASTFLASH_SIM_PORT_H
#define FASTFLASH_SIM_PORT_H

#include "fast_flash_types.h"
#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 按环境变量配置并打开模拟基座设备；返回 0 表示成功 */
int  fast_flash_sim_init_device(const char *bin_path);

/* 关闭模拟基座设备 */
void fast_flash_sim_deinit_device(void);

/* 返回当前模拟设备句柄（供统计/磨损图查询使用） */
flash_dev_t *fast_flash_sim_device(void);

/* 对接组件的全局 flash_ops_t 实例 */
extern const flash_ops_t sim_flash_ops;

#ifdef __cplusplus
}
#endif

#endif /* FASTFLASH_SIM_PORT_H */
