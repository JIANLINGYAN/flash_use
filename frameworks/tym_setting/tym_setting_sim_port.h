/**
 * tym_setting_sim_port.h - TYM Setting 组件对接模拟基座的移植层头
 *
 * 定位：TYM Setting 通过 cStorageDrv 的三个函数指针访问 Flash，
 * 移植层实现 NvmDrv_Ctor 的三个回调（读写擦）桥接 flash_sim。
 * vendor 源码经去耦裁剪（见 vendor/README.md），无其它平台依赖。
 *
 * Flash 布局：SETT_PAGE_ROM_ADDR = 0，分区容量须 >= 擦除块整数倍，
 * 由本移植层注入（默认 32KB）。
 */

#ifndef TYM_SETTING_SIM_PORT_H
#define TYM_SETTING_SIM_PORT_H

#include <stdint.h>

#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注入模拟基座设备（须在 SettingSrv_Init 之前调用）。
 * @param dev        模拟基座设备句柄
 * @param base_addr  Setting 分区起始偏移（建议 0）
 * @param capacity   分区容量（字节），须为擦除块整数倍
 * @param erase_size 擦除块大小（Bookkeeping 整页擦除粒度）
 */
void tym_setting_sim_setup(flash_dev_t *dev, uint32_t base_addr,
                           uint32_t capacity, uint32_t erase_size);

/** 返回当前注入的模拟基座设备句柄（统计用） */
flash_dev_t *tym_setting_sim_device(void);

/** 擦除整个 Setting 分区（供首次格式化使用） */
int tym_setting_sim_erase_all(void);

#ifdef __cplusplus
}
#endif

#endif /* TYM_SETTING_SIM_PORT_H */
