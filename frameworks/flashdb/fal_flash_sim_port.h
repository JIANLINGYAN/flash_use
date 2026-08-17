/**
 * fal_flash_sim_port.h - FlashDB/FAL 移植层接口
 *
 * 使用顺序（测试程序或应用侧）：
 *   1) flash_sim_init()        创建模拟介质
 *   2) fal_sim_port_init()     绑定介质 + fal_init() + 安装运行期分区表
 *   3) fdb_kvdb_init()         在分区 FAL_KVDB_PART_NAME 上创建 KVDB
 */

#ifndef FAL_FLASH_SIM_PORT_H
#define FAL_FLASH_SIM_PORT_H

#include <stdint.h>

#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 仅绑定设备与几何参数（不做 fal_init）。一般直接用 fal_sim_port_init。
 */
void fal_sim_port_setup(flash_dev_t *dev, uint32_t total_size,
                        uint32_t erase_size, int verbose);

/**
 * 完成 FAL 初始化并安装运行期 KVDB 分区。
 *
 * @param dev          已由 flash_sim_init 创建的设备句柄
 * @param total_size   介质总容量（字节）
 * @param erase_size   擦除块大小（字节）
 * @param part_offset  KVDB 分区起始偏移（须按 erase_size 对齐）
 * @param part_len     KVDB 分区长度（须 >= 2*erase_size，GC 需空闲块）
 * @param verbose      非 0 时输出 FlashDB 内部日志
 * @return             0 成功；-1 fal_init 失败；-2 分区未找到
 */
int fal_sim_port_init(flash_dev_t *dev, uint32_t total_size,
                      uint32_t erase_size, uint32_t part_offset,
                      uint32_t part_len, int verbose);

#ifdef __cplusplus
}
#endif

#endif /* FAL_FLASH_SIM_PORT_H */
