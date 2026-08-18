/**
 * spiffs_sim_port.h - SPIFFS 模拟基座移植层头文件
 *
 * 声明对接本平台模拟基座（simulator/flash_sim.c）的移植接口，供
 * SPIFFS 组件与测试程序引用。与 easyflash/ef_port.h、flashdb 的 fal
 * 头文件同一定位。
 */
#ifndef SPIFFS_SIM_PORT_H
#define SPIFFS_SIM_PORT_H

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
int spiffs_sim_init_device(const char *bin_path);

/**
 * 挂载 SPIFFS。返回 SPIFFS_OK 或错误码（全新介质会返回未格式化错误）。
 */
int spiffs_sim_mount(void);

/**
 * 格式化 SPIFFS（需已通过 init_device 打开介质）。
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

/**
 * 关闭模拟基座设备。
 */
void spiffs_sim_deinit_device(void);

/**
 * 返回当前模拟设备句柄（供统计/磨损图查询使用）。
 */
struct flash_dev *spiffs_sim_device(void);

#ifdef __cplusplus
}
#endif

#endif /* SPIFFS_SIM_PORT_H */
