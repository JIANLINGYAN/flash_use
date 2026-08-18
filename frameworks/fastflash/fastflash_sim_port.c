/**
 * fastflash_sim_port.c - fast_flashdb_table 移植层（注册式，平台无关）
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），把 fast_flashdb_table 的 flash_ops_t 移植接口桥接到统一
 * flash_hal_t（目标平台实现 read/write/erase 后注册）。
 *
 * 移植层本身不内含任何业务逻辑，仅做"组件接口 <-> 注册 HAL"的参数桥接：
 *   flash_ops_t.init   -> 校验 HAL 已注册
 *   flash_ops_t.read   -> hal->read
 *   flash_ops_t.write  -> hal->write
 *   flash_ops_t.erase  -> hal->erase
 */

#include "fastflash_sim_port.h"

/* 注册的 HAL 实例（全局单实例） */
static const flash_hal_t *s_hal = NULL;

int fast_flash_port_init(const flash_hal_t *hal)
{
    if (!hal) { return -1; }
    s_hal = hal;
    return 0;
}

/* ---- flash_ops_t 实现：桥接到注册 HAL ---- */

static int port_init(void)
{
    return s_hal ? 0 : -1;
}

static int port_read(uint32_t addr, uint8_t *buf, uint32_t size)
{
    if (!s_hal) return -1;
    return (s_hal->read(s_hal->ctx, addr, buf, size) == 0) ? 0 : -1;
}

static int port_write(uint32_t addr, const uint8_t *buf, uint32_t size)
{
    if (!s_hal) return -1;
    return (s_hal->write(s_hal->ctx, addr, buf, size) == 0) ? 0 : -1;
}

static int port_erase(uint32_t addr, uint32_t size)
{
    if (!s_hal) return -1;
    return (s_hal->erase(s_hal->ctx, addr, size) == 0) ? 0 : -1;
}

/* 供测试程序/组件引用的全局 ops 实例 */
const flash_ops_t sim_flash_ops = {
    .init  = port_init,
    .read  = port_read,
    .write = port_write,
    .erase = port_erase,
};
