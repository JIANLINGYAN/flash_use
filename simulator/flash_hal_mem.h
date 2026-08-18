/**
 * flash_hal_mem.h - 内存模拟 HAL（零依赖，平台无关）
 *
 * 为统一 flash_hal_t 提供一种"纯 RAM 介质"参考实现：
 *   - 一块 malloc 的字节数组模拟 NOR Flash：擦除=写 0xFF，写=1->0 校验；
 *   - 无需任何外部依赖（不依赖文件系统、不依赖 flash_sim）；
 *   - 用于：导出库包 demo 的 PC 冒烟验证、平台内导入库的闭环运行。
 *
 * 真实目标平台替换为 flash_hal_from_sim（平台内仿真）或直接实现
 * flash_hal_t 的 read/write/erase 对接真实驱动。
 */

#ifndef FLASH_HAL_MEM_H
#define FLASH_HAL_MEM_H

#include <stdint.h>

#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 用内存介质构造 flash_hal_t。
 * @param total     介质容量（字节）
 * @param erase     擦除块大小（字节）
 * @param hal       输出 flash_hal_t（ctx 指向内部介质）
 * @return          0 成功；-1 参数非法/内存不足
 */
int flash_hal_mem_create(uint32_t total, uint32_t erase, flash_hal_t *hal);

/**
 * 释放内存介质（hal->ctx）。
 */
void flash_hal_mem_destroy(flash_hal_t *hal);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_HAL_MEM_H */
