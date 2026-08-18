/**
 * flash_hal_adapter.h - 把模拟基座（flash_sim）桥接为统一 flash_hal_t
 *
 * 仅在平台内（PC 验证 / 应用层测试）使用：测试程序用 flash_sim_init
 * 打开介质后，通过 flash_hal_from_sim() 得到 flash_hal_t 注册给框架。
 * 导出的库本身不依赖本文件，只依赖 frameworks/common/flash_hal.h。
 */

#ifndef FLASH_HAL_ADAPTER_H
#define FLASH_HAL_ADAPTER_H

#include <stdint.h>

#include "flash_hal.h"
#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 把 flash_sim 设备句柄包装为 flash_hal_t。
 * @param dev       flash_sim_init 返回的句柄（不可为 NULL）
 * @param total     介质总容量（字节）
 * @param erase     擦除块大小（字节）
 * @param write     最小写单位（字节）
 * @param hal       输出 flash_hal_t（ctx 指向 dev）
 */
void flash_hal_from_sim(flash_dev_t *dev, uint32_t total, uint32_t erase,
                        uint32_t write, flash_hal_t *hal);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_HAL_ADAPTER_H */
