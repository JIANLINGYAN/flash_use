/**
 * fatfs_sim_port.c - FatFs 对接本平台模拟基座的移植层
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），实现 FatFs 的磁盘接口（disk_initialize/disk_status/disk_read/
 * disk_write/disk_ioctl），把"扇区（512B）读写"桥接到模拟基座的
 * flash_sim 统一抽象。
 *
 * 关键适配点：FatFs 以扇区为单位随机读写，而 flash 物理特性是"按块擦除、
 * 写入仅允许 1->0"。因此 disk_write 采用"读块->改扇区->擦块->写块"的
 * 读改写流程，保证被修改扇区所在块先被擦除再编程（符合 NOR/NAND 语义）。
 *
 * 注意：vendor 自带的 diskio.c 是依赖外部 platform.h/storage.h 的骨架，
 * 本移植层自行提供全套 disk_* 实现，编译时不再包含 vendor 的 diskio.c。
 */

#include "fatfs_sim_port.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FatFs 相关类型（避免与 flash_sim 冲突，仅在本文件使用） */
#include "ff.h"
#include "diskio.h"

#define SECTOR_SIZE  512u

/* 全局模拟设备句柄（由 fatfs_sim_init_device 建立） */
static flash_dev_t *g_sim_dev = NULL;

/* 介质擦除块大小 / 总容量（缓存，flash_sim 句柄不透明无法直接读取） */
static uint32_t s_block_size = 4096;
static uint32_t s_total_size = 128 * 1024;

/* FatFs 磁盘偏移（介质起始偏移，默认 0） */
static uint32_t s_disk_base = 0;

int fatfs_sim_init_device(const char *bin_path)
{
    flash_config_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.bin_path = bin_path ? bin_path : "fatfs_sim.bin";
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
    s_total_size = fc.total_size;
    return 0;
}

void fatfs_sim_deinit_device(void)
{
    if (g_sim_dev) { flash_sim_deinit(g_sim_dev); g_sim_dev = NULL; }
}

struct flash_dev *fatfs_sim_device(void)
{
    return g_sim_dev;
}

uint32_t fatfs_sim_sector_size(void)
{
    return SECTOR_SIZE;
}

/* FatFs 时间戳回调：无 RTC，返回固定时间（2026-01-01 00:00:00） */
DWORD get_fattime(void)
{
    /* FAT 时间戳位域：年(1980起,7bit)|月(4bit)|日(5bit)|时(5bit)|分(6bit)|秒/2(5bit) */
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16)
           | ((DWORD)0 << 11) | ((DWORD)0 << 5) | 0;
}

/* ==================== FatFs disk 接口 ==================== */

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return g_sim_dev ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return g_sim_dev ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (!g_sim_dev) { return RES_NOTRDY; }
    if (flash_sim_read(g_sim_dev, s_disk_base + sector * SECTOR_SIZE,
                       buff, count * SECTOR_SIZE) != FLASH_OK) {
        return RES_PARERR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (!g_sim_dev) { return RES_NOTRDY; }
    if (s_block_size == 0 || s_block_size % SECTOR_SIZE != 0) {
        return RES_PARERR;
    }

    uint8_t *block = (uint8_t *)malloc(s_block_size);
    if (!block) { return RES_ERROR; }

    for (UINT s = 0; s < count; s++) {
        uint32_t abs = (uint32_t)(sector + s) * SECTOR_SIZE;
        uint32_t blk_start = (abs / s_block_size) * s_block_size;
        /* 读改写：读整块 -> 改扇区 -> 擦块 -> 写块 */
        if (flash_sim_read(g_sim_dev, s_disk_base + blk_start,
                           block, s_block_size) != FLASH_OK) {
            free(block);
            return RES_ERROR;
        }
        memcpy(block + (abs % s_block_size), buff + s * SECTOR_SIZE,
               SECTOR_SIZE);
        if (flash_sim_erase(g_sim_dev, s_disk_base + blk_start,
                            s_block_size) != FLASH_OK) {
            free(block);
            return RES_ERROR;
        }
        if (flash_sim_write(g_sim_dev, s_disk_base + blk_start,
                            block, s_block_size) != FLASH_OK) {
            free(block);
            return RES_ERROR;
        }
    }
    free(block);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    if (!g_sim_dev) { return RES_NOTRDY; }
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;   /* flash_sim 每次写/擦已立即落盘 */
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = s_total_size / SECTOR_SIZE;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = s_block_size / SECTOR_SIZE;
        return RES_OK;
    case CTRL_TRIM:
        return RES_OK;   /* 忽略 trim，写前读改写已保证正确性 */
    default:
        return RES_PARERR;
    }
}
