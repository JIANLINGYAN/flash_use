/**
 * littlefs_sim_port.c - LittleFS 对接本平台模拟基座的移植层
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），把 littlefs 的块设备回调（read/prog/erase/sync）对接到
 * simulator/flash_sim.c 实现的统一 Flash 抽象。
 *
 * 移植层本身不含业务逻辑，仅做"块设备接口 <-> 模拟基座"的参数与句柄桥接：
 *   read  -> flash_sim_read
 *   prog  -> flash_sim_write
 *   erase -> flash_sim_erase
 *   sync  -> 空操作（flash_sim 每次写/擦立即落盘）
 *
 * 几何参数：block_size = 介质擦除块大小；read/prog_size = write_size；
 * cache/lookahead 取合理值（prog_size 倍数）。
 */

#include "littlefs_sim_port.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 全局模拟设备句柄（由 littlefs_sim_init_device 建立） */
static flash_dev_t *g_sim_dev = NULL;

/* 介质擦除块大小（缓存，flash_sim 句柄不透明无法直接读取） */
static uint32_t s_block_size = 4096;

/* LittleFS 块地址偏移（介质起始偏移，默认 0） */
static uint32_t s_lfs_base = 0;

static int port_read(const struct lfs_config *c, lfs_block_t block,
                     lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)c;
    if (!g_sim_dev) { return LFS_ERR_IO; }
    uint32_t addr = s_lfs_base + (uint32_t)block * (uint32_t)c->block_size
                    + (uint32_t)off;
    if (flash_sim_read(g_sim_dev, addr, buffer, size) != FLASH_OK) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int port_prog(const struct lfs_config *c, lfs_block_t block,
                     lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)c;
    if (!g_sim_dev) { return LFS_ERR_IO; }
    uint32_t addr = s_lfs_base + (uint32_t)block * (uint32_t)c->block_size
                    + (uint32_t)off;
    if (flash_sim_write(g_sim_dev, addr, buffer, size) != FLASH_OK) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int port_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    if (!g_sim_dev) { return LFS_ERR_IO; }
    uint32_t addr = s_lfs_base + (uint32_t)block * (uint32_t)c->block_size;
    if (flash_sim_erase(g_sim_dev, addr, (uint32_t)c->block_size) != FLASH_OK) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int port_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;   /* flash_sim 每次写/擦已立即落盘 */
}

int littlefs_sim_init_device(const char *bin_path, struct lfs_config *cfg)
{
    flash_config_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.bin_path = bin_path ? bin_path : "littlefs_sim.bin";
    FLASH_CFG_DEFAULTS_BY_TYPE(fc, FLASH_TYPE_NOR);

    const char *v;
#define ENV_LONG(K, D) (((v) = getenv(K)) && *v ? (uint32_t)atol(v) : (D))
    fc.type         = (flash_type_t)ENV_LONG("SIM_TYPE", FLASH_TYPE_NOR);
    fc.total_size   = ENV_LONG("SIM_TOTAL", 128 * 1024);
    fc.erase_size   = ENV_LONG("SIM_ERASE", 4096);
    fc.write_size   = ENV_LONG("SIM_WRITE", 1);
    fc.erase_cycles = ENV_LONG("SIM_CYCLES", 100000);
    fc.read_us      = ENV_LONG("SIM_RD_US", 0);
    fc.write_us     = ENV_LONG("SIM_WR_US", 0);
    fc.erase_us     = ENV_LONG("SIM_ERASE_US", 0);
    fc.bad_blocks   = ENV_LONG("SIM_BAD_N", 0);
    fc.bad_ratio    = ENV_LONG("SIM_BAD_R", 0);
#undef ENV_LONG

    if (g_sim_dev) { flash_sim_deinit(g_sim_dev); g_sim_dev = NULL; }
    g_sim_dev = flash_sim_init(&fc);
    if (!g_sim_dev) { return -1; }
    s_block_size = fc.erase_size;

    if (cfg) {
        memset(cfg, 0, sizeof(*cfg));
        cfg->context        = g_sim_dev;
        cfg->read           = port_read;
        cfg->prog           = port_prog;
        cfg->erase          = port_erase;
        cfg->sync           = port_sync;
        cfg->read_size      = fc.write_size;
        cfg->prog_size      = fc.write_size;
        cfg->block_size     = fc.erase_size;
        cfg->block_count    = fc.total_size / fc.erase_size;
        cfg->block_cycles   = 500;
        cfg->cache_size     = fc.erase_size;
        cfg->lookahead_size = fc.erase_size;
    }
    return 0;
}

void littlefs_sim_deinit_device(void)
{
    if (g_sim_dev) { flash_sim_deinit(g_sim_dev); g_sim_dev = NULL; }
}

struct flash_dev *littlefs_sim_device(void)
{
    return g_sim_dev;
}

uint32_t littlefs_sim_block_size(void)
{
    return s_block_size;
}
