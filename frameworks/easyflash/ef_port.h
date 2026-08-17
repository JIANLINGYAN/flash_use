/**
 * ef_port.h - EasyFlash 移植层参数注入接口
 *
 * 上游 EasyFlash 的区域参数（起始地址/区大小/擦除块大小）原本是编译期宏，
 * 本平台为支持"前端可配置介质与 KV 区参数"，改为运行期注入：
 * 先调用 ef_port_setup() 绑定模拟基座设备与区域参数，再调用 easyflash_init()。
 */

#ifndef EF_PORT_H
#define EF_PORT_H

#include <stdint.h>

#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 绑定底层模拟基座设备并设置 KV 区域参数。
 * 必须在 easyflash_init() 之前调用。
 *
 * @param dev         已由 flash_sim_init 创建的设备句柄
 * @param start_addr  KV(ENV) 区起始偏移，须按 erase_size 对齐
 * @param area_size   KV(ENV) 区大小，须 >= 2*erase_size（GC 需空闲块）
 * @param erase_size  擦除块大小（与模拟基座配置一致）
 * @param verbose     非 0 时输出 EasyFlash 内部日志
 */
void ef_port_setup(flash_dev_t *dev, uint32_t start_addr, uint32_t area_size,
                   uint32_t erase_size, int verbose);

#ifdef __cplusplus
}
#endif

#endif /* EF_PORT_H */
