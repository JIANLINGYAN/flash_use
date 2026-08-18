/**
 * littlefs_sim_port.c - LittleFS 移植层（注册式，平台无关）
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），把 littlefs 的块设备回调（read/prog/erase/sync）桥接到
 * 统一 flash_hal_t（目标平台实现 read/write/erase 后注册）。
 *
 * 移植层本身不含业务逻辑，仅做"块设备接口 <-> 注册 HAL"的参数桥接：
 *   read  -> hal->read
 *   prog  -> hal->write
 *   erase -> hal->erase（按块）
 *   sync  -> 空操作（底层写/擦通常立即落盘）
 *
 * 几何参数：block_size = hal->erase_size；read/prog_size = hal->write_size；
 * block_count 由分区长度与块大小计算；cache/lookahead 取合理值
 * （block_size 倍数）。目标平台只需注册 hal，本文件可原样复用。
 */

#include "littlefs_sim_port.h"

#include <string.h>

/* 注册的 HAL 实例与分区基址（全局单实例） */
static const flash_hal_t *s_hal = NULL;
static uint32_t s_base = 0;

static int port_read(const struct lfs_config *c, lfs_block_t block,
                     lfs_off_t off, void *buffer, lfs_size_t size)
{
    if (!s_hal) { return LFS_ERR_IO; }
    uint32_t addr = s_base + (uint32_t)block * (uint32_t)c->block_size
                    + (uint32_t)off;
    if (s_hal->read(s_hal->ctx, addr, buffer, size) != 0) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int port_prog(const struct lfs_config *c, lfs_block_t block,
                     lfs_off_t off, const void *buffer, lfs_size_t size)
{
    if (!s_hal) { return LFS_ERR_IO; }
    uint32_t addr = s_base + (uint32_t)block * (uint32_t)c->block_size
                    + (uint32_t)off;
    if (s_hal->write(s_hal->ctx, addr, buffer, size) != 0) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int port_erase(const struct lfs_config *c, lfs_block_t block)
{
    if (!s_hal) { return LFS_ERR_IO; }
    uint32_t addr = s_base + (uint32_t)block * (uint32_t)c->block_size;
    if (s_hal->erase(s_hal->ctx, addr, (uint32_t)c->block_size) != 0) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int port_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;   /* 底层写/擦通常立即落盘 */
}

int littlefs_port_init(const flash_hal_t *hal, uint32_t base,
                       struct lfs_config *cfg)
{
    if (!hal || !cfg || hal->erase_size == 0) { return -1; }

    s_hal = hal;
    s_base = base;

    memset(cfg, 0, sizeof(*cfg));
    cfg->context        = s_hal->ctx;
    cfg->read           = port_read;
    cfg->prog           = port_prog;
    cfg->erase          = port_erase;
    cfg->sync           = port_sync;
    cfg->read_size      = hal->write_size ? hal->write_size : 1u;
    cfg->prog_size      = hal->write_size ? hal->write_size : 1u;
    cfg->block_size     = hal->erase_size;
    cfg->block_count    = hal->total_size > base
                          ? (hal->total_size - base) / hal->erase_size : 0u;
    cfg->block_cycles   = 500;
    cfg->cache_size     = hal->erase_size;
    cfg->lookahead_size = hal->erase_size;
    return 0;
}
