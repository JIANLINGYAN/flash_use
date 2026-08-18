/**
 * yaffs_sim_port.h - YAFFS 模拟基座移植层头文件
 *
 * 声明对接本平台模拟基座（simulator/flash_sim.c）的移植接口，供
 * YAFFS 组件与测试程序引用。与 easyflash/ef_port.h、flashdb 的 fal
 * 头文件同一定位。
 */
#ifndef YAFFS_SIM_PORT_H
#define YAFFS_SIM_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模拟 NAND 几何（2K page + 32B spare、32 页/块、8 块） */
#define YAFFS_SIM_CHUNK_BYTES     2048u
#define YAFFS_SIM_SPARE_BYTES     32u
#define YAFFS_SIM_CHUNKS_PER_BLOCK 32u
#define YAFFS_SIM_BLOCKS          8u

/**
 * 按环境变量配置并打开模拟基座设备。
 * 环境变量：SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_CYCLES/SIM_RD_US/
 *           SIM_WR_US/SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R。
 * @param bin_path 介质 BIN 文件路径
 * @return         0 成功，非 0 失败
 */
int yaffs_sim_init_device(const char *bin_path);

/**
 * 启动 YAFFS（注册设备）。返回 0 成功。
 */
int yaffs_sim_start_up(void);

/**
 * 关闭模拟基座设备。
 */
void yaffs_sim_deinit_device(void);

/**
 * 返回当前模拟设备句柄（供统计/磨损图查询使用）。
 */
struct flash_dev *yaffs_sim_device(void);

#ifdef __cplusplus
}
#endif

#endif /* YAFFS_SIM_PORT_H */
