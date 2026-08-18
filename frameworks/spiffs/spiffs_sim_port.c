/**
 * spiffs_sim_port.c - SPIFFS 移植层（注册式，平台无关）
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），把 SPIFFS 的 HAL 回调（read/write/erase）桥接到统一
 * flash_hal_t（目标平台实现 read/write/erase 后注册）。
 *
 * SPIFFS 每次写/擦前由自身保证目标块已被擦除（其日志结构按"写前擦"设计），
 * 因此底层"写入仅允许 1->0"语义天然兼容。
 */

#include "spiffs_sim_port.h"

#include <string.h>

#include "spiffs.h"

/* 默认介质几何（注册时若 hal 未配置则使用） */
#define SPIFFS_TOTAL_DEFAULT  (128u * 1024)
#define SPIFFS_BLOCK_DEFAULT  4096u

/* 注册的 HAL 实例（全局单实例） */
static const flash_hal_t *s_hal = NULL;

/* SPIFFS 运行时（全局单实例） */
static spiffs g_fs;

/* 工作/查找缓冲（spiffs 要求：work=2*页、fd 区、cache 区） */
static u8_t g_work[SPIFFS_LOG_PAGE_SIZE * 2];
static u8_t g_fd_space[32 * 64];
static u8_t g_cache[SPIFFS_LOG_PAGE_SIZE * 4];

/* 介质几何缓存（来自 hal） */
static uint32_t s_total = SPIFFS_TOTAL_DEFAULT;
static uint32_t s_block = SPIFFS_BLOCK_DEFAULT;

/* ---- HAL 回调（SPIFFS 以字节地址寻址；返回非 0 即错误） ---- */
static s32_t hal_read(u32_t addr, u32_t size, u8_t *dst)
{
    return (s_hal && s_hal->read(s_hal->ctx, addr, dst, size) == 0) ? 0 : -1;
}

static s32_t hal_write(u32_t addr, u32_t size, u8_t *src)
{
    return (s_hal && s_hal->write(s_hal->ctx, addr, src, size) == 0) ? 0 : -1;
}

static s32_t hal_erase(u32_t addr, u32_t size)
{
    return (s_hal && s_hal->erase(s_hal->ctx, addr, size) == 0) ? 0 : -1;
}

int spiffs_port_init(const flash_hal_t *hal)
{
    if (!hal) { return -1; }
    s_hal = hal;
    if (hal->total_size) { s_total = hal->total_size; }
    if (hal->erase_size) { s_block = hal->erase_size; }
    return 0;
}

/* 填充并挂载 spiffs；返回 SPIFFS_OK 或错误码 */
s32_t spiffs_sim_mount(void)
{
    spiffs_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.hal_read_f     = hal_read;
    cfg.hal_write_f    = hal_write;
    cfg.hal_erase_f    = hal_erase;
    cfg.phys_size      = s_total;
    cfg.phys_addr      = 0;
    cfg.phys_erase_block = s_block;
    cfg.log_block_size = s_block;
    cfg.log_page_size  = SPIFFS_LOG_PAGE_SIZE;

    return SPIFFS_mount(&g_fs, &cfg, g_work, g_fd_space, sizeof(g_fd_space),
                        g_cache, sizeof(g_cache), 0);
}

/* 格式化（需在 mount 前或卸载后调用） */
s32_t spiffs_sim_format(void)
{
    return SPIFFS_format(&g_fs);
}

/* 卸载 */
void spiffs_sim_unmount(void)
{
    SPIFFS_unmount(&g_fs);
}

/* 返回内部 spiffs 实例（供测试程序操作文件） */
void *spiffs_sim_fs(void)
{
    return &g_fs;
}
