/**
 * nvdm_sim_port.h - Airoha NVDM 组件对接本平台模拟基座的移植层头
 *
 * 定位：NVDM 通过 nvdm_port.h 声明的 ~20 个 nvdm_port_* 函数访问硬件/OS，
 * 本移植层把这些契约函数桥接到统一 flash_hal_t（目标平台实现
 * read/write/erase 后注册），vendor/ 源码零修改。
 *
 * 分区配置：NVDM 支持多个分区，本平台统一使用 1 个分区
 * （起始偏移 0，容量由测试程序指定，须为擦除块整数倍且 >= 2 块）。
 */

#ifndef NVDM_SIM_PORT_H
#define NVDM_SIM_PORT_H

#include <stdint.h>

#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册 HAL 并注入 NVDM 分区配置（须在 nvdm_init 之前调用）。
 * @param hal        统一 flash_hal_t（实现 read/write/erase）
 * @param base_addr  分区起始偏移（相对介质，建议 0）
 * @param capacity   分区容量（字节），须为 peb_size 整数倍且 >= 2*peb_size
 * @param peb_size   物理擦除块大小（须等于 hal->erase_size）
 * @param item_count 本分区允许的最大数据项数量（决定 RAM 项头镜像大小）
 */
void nvdm_sim_setup(const flash_hal_t *hal, uint32_t base_addr, uint32_t capacity,
                    uint32_t peb_size, uint32_t item_count);

/** 重置 NVDM 全局控制块（模拟设备重启后重新 init；仅测试用） */
void nvdm_sim_reset(void);

/** 返回当前注入的 HAL 设备上下文（统计用；已注册式则返回 ctx） */
void *nvdm_sim_device(void);

#ifdef __cplusplus
}
#endif

#endif /* NVDM_SIM_PORT_H */
