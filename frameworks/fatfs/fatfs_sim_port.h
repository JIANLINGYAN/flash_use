/**
 * fatfs_sim_port.h - FatFs 模拟基座移植层头文件
 *
 * 声明对接本平台模拟基座（simulator/flash_sim.c）的移植接口，供
 * FatFs 组件与测试程序引用。与 easyflash/ef_port.h、flashdb 的 fal
 * 头文件同一定位。
 */
#ifndef FATFS_SIM_PORT_H
#define FATFS_SIM_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 按环境变量配置并打开模拟基座设备。
 * 环境变量：SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_CYCLES/SIM_RD_US/
 *           SIM_WR_US/SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R。
 * @param bin_path 介质 BIN 文件路径
 * @return         0 成功，非 0 失败
 */
int fatfs_sim_init_device(const char *bin_path);

/**
 * 关闭模拟基座设备。
 */
void fatfs_sim_deinit_device(void);

/**
 * 返回当前模拟设备句柄（供统计/磨损图查询使用）。
 */
struct flash_dev *fatfs_sim_device(void);

/**
 * 返回介质扇区大小（FatFs 以扇区为基本读写单位）。
 */
uint32_t fatfs_sim_sector_size(void);

#ifdef __cplusplus
}
#endif

#endif /* FATFS_SIM_PORT_H */
