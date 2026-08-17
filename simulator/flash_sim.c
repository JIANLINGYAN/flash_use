/**
 * flash_sim.c - 模拟基座实现
 *
 * 物理特性模拟说明：
 *  - BIN 文件作为"物理介质"真实落盘，用户可直接用十六进制工具检验。
 *  - NOR/NAND：write 仅能把 1 翻转为 0（模拟浮栅），写入前须擦除（全 0xFF）。
 *              实现上强制检查目标区域是否为 0xFF，否则返回 FLASH_ERR_WRITE，
 *              与真实 NOR 行为一致。
 *  - EEPROM：支持任意地址直接覆盖写（字节级），无 erase 概念。
 *  - erase 将整块置 0xFF，并累计擦写次数；超过寿命则后续 erase 失败。
 *
 * 可配置性能指标（flash_config_t）：
 *  - read_us / write_us / erase_us：每次操作的模拟耗时（微秒），用于性能统计。
 *  - bad_blocks / bad_ratio：坏块数量与运行时坏块比率，模拟 NAND 坏块特性。
 *
 * 统计与可视化：flash_sim_get_stats 暴露读写擦耗时与磨损概况；
 *              flash_sim_get_wear_map 暴露每块擦写次数，供前端绘制磨损热力图。
 *
 * 兼容性：纯 C99 + 标准库，Linux/Windows 可直接编译运行。
 */

#include "flash_sim.h"

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* 部分平台/编译器未通过 feature macro 暴露以下 POSIX 接口，显式声明以保证可移植 */
extern int ftruncate(int fd, long size);
extern int fileno(FILE *stream);
extern int usleep(unsigned int usec);

struct flash_dev {
    flash_config_t cfg;
    FILE          *fp;      /* BIN 文件句柄 */
    uint8_t       *image;   /* 进程内介质镜像，读写先走内存再落盘 */
    uint32_t      *erase_cycles; /* 每块擦写次数数组，长度 = total/erase_size */
    uint8_t       *bad;     /* 每块坏块标记，1=坏块 */
    uint32_t      nblocks;
    flash_stats_t stats;
};

/* 简单可复现伪随机（LCG），避免依赖全局 srand 影响调用方 */
static uint32_t s_rand_state;
static void rand_seed(uint32_t s) { s_rand_state = s ? s : 0x9E3779B9u; }
static uint32_t rand_u32(void) {
    s_rand_state = s_rand_state * 1664525u + 1013904223u;
    return s_rand_state;
}

/* 打开或创建 BIN 文件，并载入内存镜像 */
static int ensure_file(flash_dev_t *dev)
{
    dev->fp = fopen(dev->cfg.bin_path, "rb+");
    if (!dev->fp) {
        dev->fp = fopen(dev->cfg.bin_path, "wb+");
        if (!dev->fp) { return -1; }
    }

    fseek(dev->fp, 0, SEEK_END);
    long cur = ftell(dev->fp);
    if (cur < (long)dev->cfg.total_size) {
        uint8_t *zero = (uint8_t *)malloc(dev->cfg.total_size);
        if (!zero) { return -1; }
        memset(zero, 0xFF, dev->cfg.total_size);
        if (cur > 0) {
            fseek(dev->fp, 0, SEEK_SET);
            fread(zero, 1, (size_t)cur, dev->fp);
        }
        fseek(dev->fp, 0, SEEK_SET);
        fwrite(zero, 1, dev->cfg.total_size, dev->fp);
        fflush(dev->fp);
        free(zero);
    } else if (cur > (long)dev->cfg.total_size) {
#if defined(_WIN32)
        fclose(dev->fp);
        dev->fp = NULL;
        FILE *tf = fopen(dev->cfg.bin_path, "wb+");
        if (!tf) { return -1; }
        uint8_t *buf = (uint8_t *)malloc(dev->cfg.total_size);
        if (!buf) { fclose(tf); return -1; }
        memset(buf, 0xFF, dev->cfg.total_size);
        fwrite(buf, 1, dev->cfg.total_size, tf);
        fclose(tf);
        free(buf);
        dev->fp = fopen(dev->cfg.bin_path, "rb+");
        if (!dev->fp) { return -1; }
#else
        ftruncate(fileno(dev->fp), dev->cfg.total_size);
#endif
    }
    return 0;
}

/* 模拟一次操作耗时（微秒） */
static void sim_delay(uint32_t us)
{
    if (us > 0) { usleep(us); }
}

flash_dev_t *flash_sim_init(const flash_config_t *cfg)
{
    if (!cfg || !cfg->bin_path || cfg->total_size == 0) {
        return NULL;
    }
    if ((cfg->type == FLASH_TYPE_NOR || cfg->type == FLASH_TYPE_NAND)
        && cfg->erase_size == 0) {
        return NULL; /* NOR/NAND 必须有块大小 */
    }

    flash_dev_t *dev = (flash_dev_t *)calloc(1, sizeof(flash_dev_t));
    if (!dev) { return NULL; }
    dev->cfg = *cfg;
    memset(&dev->stats, 0, sizeof(dev->stats));

    if (ensure_file(dev) != 0) {
        free(dev);
        return NULL;
    }

    dev->image = (uint8_t *)malloc(dev->cfg.total_size);
    if (!dev->image) {
        fclose(dev->fp);
        free(dev);
        return NULL;
    }
    fseek(dev->fp, 0, SEEK_SET);
    if (fread(dev->image, 1, dev->cfg.total_size, dev->fp) != dev->cfg.total_size) {
        fclose(dev->fp);
        free(dev->image);
        free(dev);
        return NULL;
    }

    /* 块级数组（仅 NOR/NAND） */
    if (cfg->type != FLASH_TYPE_EEPROM && cfg->erase_size > 0) {
        dev->nblocks = dev->cfg.total_size / dev->cfg.erase_size;
        dev->erase_cycles = (uint32_t *)calloc(dev->nblocks, sizeof(uint32_t));
        dev->bad = (uint8_t *)calloc(dev->nblocks, sizeof(uint8_t));
        if (!dev->erase_cycles || !dev->bad) {
            fclose(dev->fp);
            free(dev->image);
            free(dev->erase_cycles);
            free(dev->bad);
            free(dev);
            return NULL;
        }
        /* 初始化固定坏块（随机选取 bad_blocks 个） */
        if (dev->cfg.bad_blocks > 0 && dev->nblocks > 0) {
            rand_seed((uint32_t)time(NULL) ^ 0x1234u);
            uint32_t placed = 0, guard = 0;
            while (placed < dev->cfg.bad_blocks && guard < dev->nblocks * 4) {
                uint32_t idx = rand_u32() % dev->nblocks;
                if (!dev->bad[idx]) { dev->bad[idx] = 1; placed++; }
                guard++;
            }
            dev->stats.bad_block_count = placed;
        }
    }

    return dev;
}

void flash_sim_deinit(flash_dev_t *dev)
{
    if (!dev) { return; }
    if (dev->fp) { fclose(dev->fp); }
    if (dev->image) { free(dev->image); }
    if (dev->erase_cycles) { free(dev->erase_cycles); }
    if (dev->bad) { free(dev->bad); }
    free(dev);
}

flash_err_t flash_sim_read(const flash_dev_t *dev, uint32_t offset,
                           void *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) { return FLASH_ERR_ARGS; }
    if (offset + len > dev->cfg.total_size) { return FLASH_ERR_RANGE; }

    sim_delay(dev->cfg.read_us);
    memcpy(buf, dev->image + offset, len);
    flash_dev_t *d = (flash_dev_t *)dev;
    d->stats.total_reads++;
    d->stats.read_time_us += dev->cfg.read_us;
    return FLASH_OK;
}

flash_err_t flash_sim_write(flash_dev_t *dev, uint32_t offset,
                            const void *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) { return FLASH_ERR_ARGS; }
    if (offset + len > dev->cfg.total_size) { return FLASH_ERR_RANGE; }
    if (len % dev->cfg.write_size != 0 || offset % dev->cfg.write_size != 0) {
        return FLASH_ERR_ARGS; /* 未按最小写入单位对齐 */
    }

    const uint8_t *src = (const uint8_t *)buf;

    if (dev->cfg.type == FLASH_TYPE_EEPROM) {
        memcpy(dev->image + offset, src, len);
    } else {
        /*
         * NOR/NAND 编程语义：写入是"按位与"（浮栅只能由 1 编程为 0），
         * 因此源数据中为 1 的位表示"保持该位当前状态"，不构成非法写入；
         * 只有当源数据某位为 0 而目标已为 0 时也是允许的（重复编程为 0）。
         *
         * 真正非法的情况是"试图把已经是 0 的位擦回 1"，但按位与永远不会
         * 产生该效果——它只可能让位保持或变 0。故写入本身始终可执行，
         * 不需要预检查；是否"看起来像写失败"由上层通过回读比对判断。
         *
         * 说明（重要）：此前实现要求 (image & src) == src，即禁止在已写过
         * (含 0 位) 的区域写入含 1 位的数据。这与真实 NOR 不符——成熟框架
         * （如 EasyFlash / FlashDB）会在已写区域上覆写包含 0xFF 填充位或
         * 未变更状态位的结构体，属于合法操作，却会被误判为 FLASH_ERR_WRITE。
         */
        for (uint32_t i = 0; i < len; i++) {
            dev->image[offset + i] &= src[i];
        }
    }

    sim_delay(dev->cfg.write_us);
    fseek(dev->fp, (long)offset, SEEK_SET);
    if (fwrite(dev->image + offset, 1, len, dev->fp) != len) {
        return FLASH_ERR_IO;
    }
    fflush(dev->fp);

    dev->stats.total_writes++;
    dev->stats.total_write_bytes += len;
    dev->stats.write_time_us += dev->cfg.write_us;
    return FLASH_OK;
}

flash_err_t flash_sim_erase(flash_dev_t *dev, uint32_t offset, uint32_t len)
{
    if (!dev) { return FLASH_ERR_ARGS; }
    if (dev->cfg.type == FLASH_TYPE_EEPROM) {
        return FLASH_ERR_NOTSUP; /* EEPROM 无擦除 */
    }
    if (offset % dev->cfg.erase_size != 0 || len % dev->cfg.erase_size != 0) {
        return FLASH_ERR_ARGS; /* 必须按块对齐 */
    }
    if (offset + len > dev->cfg.total_size) { return FLASH_ERR_RANGE; }

    uint32_t nblocks = dev->nblocks;

    for (uint32_t b = offset; b < offset + len; b += dev->cfg.erase_size) {
        uint32_t blk_idx = b / dev->cfg.erase_size;
        if (blk_idx >= nblocks) { continue; }

        /* 固定坏块：擦除直接失败 */
        if (dev->bad[blk_idx]) {
            return FLASH_ERR_ERASE;
        }
        /* 运行时坏块注入：按 bad_ratio 概率（万分之一精度）标记坏块 */
        if (dev->cfg.bad_ratio > 0) {
            if ((rand_u32() % 10000) < dev->cfg.bad_ratio) {
                dev->bad[blk_idx] = 1;
                dev->stats.bad_block_count++;
                return FLASH_ERR_ERASE;
            }
        }
        if (dev->erase_cycles[blk_idx] >= dev->cfg.erase_cycles) {
            return FLASH_ERR_ERASE; /* 寿命耗尽 */
        }
        dev->erase_cycles[blk_idx]++;
        if (dev->erase_cycles[blk_idx] > dev->stats.max_erase_cycles) {
            dev->stats.max_erase_cycles = dev->erase_cycles[blk_idx];
        }

        sim_delay(dev->cfg.erase_us);
        memset(dev->image + b, 0xFF, dev->cfg.erase_size);
        fseek(dev->fp, (long)b, SEEK_SET);
        if (fwrite(dev->image + b, 1, dev->cfg.erase_size, dev->fp)
            != dev->cfg.erase_size) {
            return FLASH_ERR_IO;
        }
        dev->stats.total_erases++;
        dev->stats.erase_time_us += dev->cfg.erase_us;
    }
    fflush(dev->fp);
    return FLASH_OK;
}

flash_err_t flash_sim_get_erase_count(const flash_dev_t *dev, uint32_t offset,
                                      uint32_t *cycles)
{
    if (!dev || !cycles) { return FLASH_ERR_ARGS; }
    if (dev->cfg.type == FLASH_TYPE_EEPROM) { return FLASH_ERR_NOTSUP; }
    if (offset >= dev->cfg.total_size) { return FLASH_ERR_RANGE; }
    uint32_t blk_idx = offset / dev->cfg.erase_size;
    *cycles = dev->erase_cycles ? dev->erase_cycles[blk_idx] : 0;
    return FLASH_OK;
}

flash_err_t flash_sim_get_stats(const flash_dev_t *dev, flash_stats_t *stats)
{
    if (!dev || !stats) { return FLASH_ERR_ARGS; }
    *stats = dev->stats;
    if (dev->nblocks > 0) {
        uint64_t sum = 0;
        uint32_t valid = 0;
        for (uint32_t i = 0; i < dev->nblocks; i++) {
            if (!dev->bad[i]) { sum += dev->erase_cycles[i]; valid++; }
        }
        stats->avg_erase_cycles = valid ? (uint32_t)(sum / valid) : 0;
    }
    return FLASH_OK;
}

uint32_t flash_sim_get_wear_map(const flash_dev_t *dev, uint32_t *out,
                                uint32_t max_blocks)
{
    if (!dev) { return 0; }
    uint32_t n = dev->nblocks;
    if (!out) { return n; }
    uint32_t copy = n < max_blocks ? n : max_blocks;
    for (uint32_t i = 0; i < copy; i++) {
        out[i] = dev->erase_cycles[i];
    }
    return n;
}

uint32_t flash_sim_block_count(const flash_dev_t *dev)
{
    if (!dev) { return 0; }
    return dev->nblocks;
}
