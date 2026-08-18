/**
 * fal_flash_sim_port.c - FlashDB/FAL 移植层：对接本平台模拟基座 flash_sim
 *
 * FAL（Flash Abstraction Layer）要求提供一个 fal_flash_dev，内含
 * init/read/write/erase 四个操作。本文件把它们直接映射到 flash_sim 接口，
 * 因此 vendor/ 下的 FlashDB 与 FAL 源码可保持零修改。
 *
 * 地址语义：FAL 传入 ops 的 offset 是"相对本 flash 设备起始的偏移"，
 * 而 flash_sim 的 offset 是"介质绝对偏移"。本平台让设备起始 addr=0、
 * len=介质总容量，二者恰好一致，故直接透传。
 *
 * 移植到真实 MCU：仅需把下面三处 flash_sim_* 调用替换为真实驱动即可。
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <fal.h>

#include "fal_flash_sim_port.h"
#include "flash_hal.h"

/* 统一 HAL 注册实例（由 fal_sim_port_setup 注入） */
static const flash_hal_t *s_hal;
static uint32_t     s_verbose;

/* 运行期 KVDB 分区表（替换 fal_cfg.h 中的编译期占位表） */
static struct fal_partition s_parts[1];

/* ---- FAL 设备操作实现 ---- */

static int sim_flash_init(void)
{
    return 0; /* 介质已由调用方注册 HAL，无需额外动作 */
}

static int sim_flash_read(long offset, uint8_t *buf, size_t size)
{
    if (!s_hal || size == 0) { return (int)size; }
    if (s_hal->read(s_hal->ctx, (uint32_t)offset, buf, (uint32_t)size) != 0) {
        return -1;
    }
    return (int)size;
}

static int sim_flash_write(long offset, const uint8_t *buf, size_t size)
{
    if (!s_hal || size == 0) { return (int)size; }
    if (s_hal->write(s_hal->ctx, (uint32_t)offset, buf, (uint32_t)size) != 0) {
        return -1;
    }
    return (int)size;
}

static int sim_flash_erase(long offset, size_t size)
{
    if (!s_hal || size == 0) { return (int)size; }

    /*
     * FAL/FlashDB 传入的 size 可能非块大小整数倍，而 HAL 要求按块对齐，
     * 故向上取整到整块（等价于真实驱动"擦除覆盖该范围的所有块"）。
     */
    uint32_t blk = flash_sim_dev.blk_size ? (uint32_t)flash_sim_dev.blk_size : 1u;
    uint32_t aligned = (uint32_t)size;
    uint32_t rem = aligned % blk;
    if (rem != 0) { aligned += blk - rem; }

    if (s_hal->erase(s_hal->ctx, (uint32_t)offset, aligned) != 0) {
        return -1;
    }
    return (int)size;
}

/*
 * FAL Flash 设备实例。name 必须与 fal_cfg.h 分区表中的 flash_name 一致。
 * addr/len/blk_size 于运行期由 fal_sim_port_setup() 填充。
 * write_gran=1 表示字节写粒度（NOR）。
 */
struct fal_flash_dev flash_sim_dev = {
    .name = "flash_sim",
    .addr = 0,
    .len = 0,
    .blk_size = 0,
    .ops = {
        .init = sim_flash_init,
        .read = sim_flash_read,
        .write = sim_flash_write,
        .erase = sim_flash_erase,
    },
    .write_gran = 1,
};

/* FlashDB 日志输出重定向（由 fdb_cfg.h 的 FDB_PRINT 指向此处） */
void fdb_sim_print(const char *fmt, ...)
{
    if (!s_verbose) { return; }
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void fal_sim_port_setup(const flash_hal_t *hal, uint32_t total_size,
                        uint32_t erase_size, int verbose)
{
    s_hal = hal;
    s_verbose = verbose ? 1u : 0u;

    flash_sim_dev.addr = 0;
    flash_sim_dev.len = total_size;
    flash_sim_dev.blk_size = erase_size;
}

int fal_sim_port_init(const flash_hal_t *hal, uint32_t total_size,
                      uint32_t erase_size, uint32_t part_offset,
                      uint32_t part_len, int verbose)
{
    fal_sim_port_setup(hal, total_size, erase_size, verbose);

    /* 初始化 FAL（会加载 fal_cfg.h 中的设备表与占位分区表） */
    if (fal_init() < 0) {
        return -1;
    }

    /* 用运行期计算出的真实分区参数替换占位分区表 */
    memset(s_parts, 0, sizeof(s_parts));
    s_parts[0].magic_word = FAL_PART_MAGIC_WORD;
    snprintf(s_parts[0].name, FAL_DEV_NAME_MAX, "%s", FAL_KVDB_PART_NAME);
    snprintf(s_parts[0].flash_name, FAL_DEV_NAME_MAX, "%s", "flash_sim");
    s_parts[0].offset = (long)part_offset;
    s_parts[0].len = part_len;

    fal_set_partition_table_temp(s_parts, 1);

    if (fal_partition_find(FAL_KVDB_PART_NAME) == NULL) {
        return -2;
    }
    return 0;
}
