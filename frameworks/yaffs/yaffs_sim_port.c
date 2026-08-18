/**
 * yaffs_sim_port.c - YAFFS 对接本平台模拟基座的移植层
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），为 YAFFS Direct（用户空间接口）提供两块胶水：
 *
 * 1) yaffsfs_* 操作系统胶水（锁/时间/内存/错误），见 yaffs_osglue.h；
 * 2) 低层 NAND driver 回调（yaffs_driver），把"chunk 数据 + oob"的读写
 *    映射到 flash_sim 线性地址空间：
 *       chunk 地址 = nand_chunk * (chunk_size + spare_size)
 *       data 区    = [chunk_addr, chunk_addr + chunk_size)
 *       oob 区     = [chunk_addr + chunk_size, chunk_addr + chunk_size + spare)
 *    YAFFS2 内部自动使用 yaffs_tags_marshall 把 tags 打包进 oob，并对
 *    已擦除块顺序编程（符合 flash 仅允许 1->0 语义）。
 *
 * 移植层不含业务逻辑，仅做接口桥接。
 */

#include "yaffs_sim_port.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* loff_t/dev_t 由 yaffs_host_types.h 提供（-include 注入所有 vendor 源） */
#include "yaffsfs.h"
#include "yaffs_guts.h"
#include "yaffscfg.h"
#include "yaffs_trace.h"

/* yaffs_trace_mask：编译期关闭全部 trace（0 = 不输出） */
unsigned int yaffs_trace_mask;

/* yaffsfs_GetLastError：返回最近一次 yaffsfs_SetError 记录的错误 */
static int s_last_error;

void yaffsfs_SetError(int err)
{
    s_last_error = err;
}

int yaffsfs_GetLastError(void)
{
    return s_last_error;
}

/* 全局模拟设备句柄（由 yaffs_sim_init_device 建立） */
static flash_dev_t *g_sim_dev = NULL;

/* YAFFS 设备（静态单实例） */
static struct yaffs_dev g_yaffs_dev;

/* 介质总字节 / 块字节 */
static uint32_t s_media_total = 0;
static uint32_t s_block_bytes = 0;

/* ---------------- OS 胶水（yaffs_osglue.h 要求） ---------------- */

void yaffsfs_Lock(void)
{
    /* 单线程环境，无需加锁 */
}

void yaffsfs_Unlock(void)
{
}

u32 yaffsfs_CurrentTime(void)
{
    /* 无 RTC：返回固定时间 2026-01-01 00:00:00 */
    return (u32)1767225600u;
}

void *yaffsfs_malloc(size_t size)
{
    return malloc(size);
}

void yaffsfs_free(void *addr)
{
    free(addr);
}

int yaffsfs_CheckMemRegion(const void *addr, size_t size, int write_request)
{
    (void)addr;
    (void)size;
    (void)write_request;
    return 1;   /* 宿主进程内存总是可访问 */
}

void yaffsfs_OSInitialisation(void)
{
    /* 无 OS 层初始化 */
}

void yaffs_bug_fn(const char *file_name, int line_no)
{
    fprintf(stderr, "YAFFS BUG: %s:%d\n", file_name, line_no);
}

/* ---------------- NAND driver 回调（yaffs_driver） ---------------- */

static int drv_initialise(struct yaffs_dev *dev)
{
    (void)dev;
    return YAFFS_OK;   /* 介质已在 yaffs_sim_init_device 打开 */
}

static int drv_deinitialise(struct yaffs_dev *dev)
{
    (void)dev;
    return YAFFS_OK;
}

static int drv_write_chunk(struct yaffs_dev *dev, int nand_chunk,
                           const u8 *data, int data_len,
                           const u8 *oob, int oob_len)
{
    (void)dev;
    uint32_t base = (uint32_t)nand_chunk
                    * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES);
    if (data && data_len > 0) {
        if (flash_sim_write(g_sim_dev, base, data, (uint32_t)data_len)
            != FLASH_OK) {
            return YAFFS_FAIL;
        }
    }
    if (oob && oob_len > 0) {
        if (flash_sim_write(g_sim_dev, base + YAFFS_SIM_CHUNK_BYTES,
                            oob, (uint32_t)oob_len) != FLASH_OK) {
            return YAFFS_FAIL;
        }
    }
    return YAFFS_OK;
}

static int drv_read_chunk(struct yaffs_dev *dev, int nand_chunk,
                          u8 *data, int data_len,
                          u8 *oob, int oob_len,
                          enum yaffs_ecc_result *ecc_result)
{
    (void)dev;
    uint32_t base = (uint32_t)nand_chunk
                    * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES);
    if (data && data_len > 0) {
        if (flash_sim_read(g_sim_dev, base, data, (uint32_t)data_len)
            != FLASH_OK) {
            return YAFFS_FAIL;
        }
    }
    if (oob && oob_len > 0) {
        if (flash_sim_read(g_sim_dev, base + YAFFS_SIM_CHUNK_BYTES,
                           oob, (uint32_t)oob_len) != FLASH_OK) {
            return YAFFS_FAIL;
        }
    }
    if (ecc_result) {
        *ecc_result = YAFFS_ECC_RESULT_NO_ERROR;   /* 模拟介质无位翻转 */
    }
    return YAFFS_OK;
}

static int drv_erase_block(struct yaffs_dev *dev, int block_no)
{
    (void)dev;
    uint32_t addr = (uint32_t)block_no * s_block_bytes;
    if (flash_sim_erase(g_sim_dev, addr, s_block_bytes) != FLASH_OK) {
        return YAFFS_FAIL;
    }
    return YAFFS_OK;
}

static int drv_mark_bad(struct yaffs_dev *dev, int block_no)
{
    /* 模拟介质不产生坏块；预留接口，直接成功 */
    (void)dev;
    (void)block_no;
    return YAFFS_OK;
}

static int drv_check_bad(struct yaffs_dev *dev, int block_no)
{
    (void)dev;
    (void)block_no;
    return YAFFS_OK;   /* 非坏块 */
}

/* ---------------- 设备启动 ---------------- */

int yaffs_sim_init_device(const char *bin_path)
{
    flash_config_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.bin_path = bin_path ? bin_path : "yaffs_sim.bin";
    FLASH_CFG_DEFAULTS_BY_TYPE(fc, FLASH_TYPE_NOR);

    const char *v;
#define ENV_LONG(K, D) (((v) = getenv(K)) && *v ? (uint32_t)atol(v) : (D))
    fc.type         = (flash_type_t)ENV_LONG("SIM_TYPE", FLASH_TYPE_NOR);
    fc.total_size   = ENV_LONG("SIM_TOTAL", YAFFS_SIM_BLOCKS
                               * YAFFS_SIM_CHUNKS_PER_BLOCK
                               * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES));
    fc.erase_size   = ENV_LONG("SIM_ERASE", YAFFS_SIM_CHUNKS_PER_BLOCK
                               * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES));
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

    s_block_bytes = fc.erase_size;
    s_media_total = fc.total_size;

    /* 保证介质几何与 YAFFS 布局一致 */
    if (s_block_bytes != YAFFS_SIM_CHUNKS_PER_BLOCK
                         * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES)) {
        flash_sim_deinit(g_sim_dev);
        g_sim_dev = NULL;
        return -2;
    }
    return 0;
}

int yaffs_sim_start_up(void)
{
    memset(&g_yaffs_dev, 0, sizeof(g_yaffs_dev));

    g_yaffs_dev.param.name = "/flash";
    g_yaffs_dev.param.start_block = 0;
    g_yaffs_dev.param.end_block = YAFFS_SIM_BLOCKS - 1;
    g_yaffs_dev.param.total_bytes_per_chunk = YAFFS_SIM_CHUNK_BYTES;
    g_yaffs_dev.param.spare_bytes_per_chunk = YAFFS_SIM_SPARE_BYTES;
    g_yaffs_dev.param.chunks_per_block = YAFFS_SIM_CHUNKS_PER_BLOCK;
    g_yaffs_dev.param.n_reserved_blocks = 2;
    g_yaffs_dev.param.n_caches = 10;
    g_yaffs_dev.param.is_yaffs2 = 1;
    g_yaffs_dev.param.use_nand_ecc = 0;   /* 模拟介质由 tags_marshall 做内嵌校验 */
    g_yaffs_dev.param.no_tags_ecc = 1;    /* 关闭 tags ECC，简化 spare 布局 */
    g_yaffs_dev.param.inband_tags = 0;
    g_yaffs_dev.param.max_objects = YAFFS_SIM_CHUNKS_PER_BLOCK * YAFFS_SIM_BLOCKS;
    g_yaffs_dev.param.disable_lazy_load = 0;   /* 启用 lazy load */
    g_yaffs_dev.param.stored_endian = 1;  /* little-endian */

    g_yaffs_dev.drv.drv_initialise_fn   = drv_initialise;
    g_yaffs_dev.drv.drv_deinitialise_fn = drv_deinitialise;
    g_yaffs_dev.drv.drv_write_chunk_fn  = drv_write_chunk;
    g_yaffs_dev.drv.drv_read_chunk_fn   = drv_read_chunk;
    g_yaffs_dev.drv.drv_erase_fn        = drv_erase_block;
    g_yaffs_dev.drv.drv_mark_bad_fn     = drv_mark_bad;
    g_yaffs_dev.drv.drv_check_bad_fn    = drv_check_bad;

    yaffs_add_device(&g_yaffs_dev);   /* 返回 void */
    return 0;
}

void yaffs_sim_deinit_device(void)
{
    if (g_sim_dev) { flash_sim_deinit(g_sim_dev); g_sim_dev = NULL; }
}

struct flash_dev *yaffs_sim_device(void)
{
    return g_sim_dev;
}
