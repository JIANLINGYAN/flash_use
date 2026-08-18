/**
 * ef_port.h - EasyFlash 移植层参数注入接口（注册式，平台无关）
 *
 * 上游 EasyFlash 的区域参数（起始地址/区大小/擦除块大小）原本是编译期宏，
 * 本平台为支持"前端可配置介质与 KV 区参数"，改为运行期注入：
 * 先调用 ef_port_setup() 注册统一 flash_hal_t 与区域参数，再调用 easyflash_init()。
 */

#ifndef EF_PORT_H
#define EF_PORT_H

#include <stdint.h>

#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册统一 flash_hal_t 并设置 KV 区域参数。
 * 必须在 easyflash_init() 之前调用。
 *
 * @param hal         已注册的 flash_hal_t（实现 read/write/erase）
 * @param start_addr  KV(ENV) 区起始偏移，须按 erase_size 对齐
 * @param area_size   KV(ENV) 区大小，须 >= 2*erase_size（GC 需空闲块）
 * @param erase_size  擦除块大小（与 hal->erase_size 一致）
 * @param verbose     非 0 时输出 EasyFlash 内部日志
 */
void ef_port_setup(const flash_hal_t *hal, uint32_t start_addr, uint32_t area_size,
                   uint32_t erase_size, int verbose);

#ifdef __cplusplus
}
#endif

#endif /* EF_PORT_H */
