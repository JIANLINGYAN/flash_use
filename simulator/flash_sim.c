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
 * 兼容性：纯 C99 + 标准库，Windows(Linux 同理) 可直接编译运行。
 */

#include "flash_sim.h"

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 部分平台/编译器未通过 feature macro 暴露以下 POSIX 接口，显式声明以保证可移植 */
extern int ftruncate(int fd, long size);
extern int fileno(FILE *stream);

struct flash_dev {
    flash_config_t cfg;
    FILE          *fp;      /* BIN 文件句柄 */
    uint8_t       *image;   /* 进程内介质镜像，读写先走内存再落盘 */
    uint32_t      *erase_cycles; /* 每块擦写次数数组，长度 = total/erase_size */
    flash_stats_t stats;
};

/* 打开或创建 BIN 文件，并载入内存镜像 */
static int ensure_file(flash_dev_t *dev)
{
    /* 以读写二进制打开；不存在则创建 */
    dev->fp = fopen(dev->cfg.bin_path, "rb+");
    if (!dev->fp) {
        dev->fp = fopen(dev->cfg.bin_path, "wb+");
        if (!dev->fp) {
            return -1;
        }
    }

    /* 若文件大小不足，扩展为 total_size（以 0xFF 填充，模拟空 Flash） */
    fseek(dev->fp, 0, SEEK_END);
    long cur = ftell(dev->fp);
    if (cur < (long)dev->cfg.total_size) {
        uint8_t *zero = (uint8_t *)malloc(dev->cfg.total_size);
        if (!zero) { return -1; }
        memset(zero, 0xFF, dev->cfg.total_size);
        /* 保留已有内容 */
        if (cur > 0) {
            fseek(dev->fp, 0, SEEK_SET);
            fread(zero, 1, (size_t)cur, dev->fp);
        }
        fseek(dev->fp, 0, SEEK_SET);
        fwrite(zero, 1, dev->cfg.total_size, dev->fp);
        fflush(dev->fp);
        free(zero);
    } else if (cur > (long)dev->cfg.total_size) {
        /* 截断到配置大小 */
#if defined(_WIN32)
        fclose(dev->fp);
        dev->fp = NULL;
        /* Windows 下用 _chsize_s 通过 freopen 后不便，简单重建 */
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

    /* 载入内存镜像 */
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

    /* 擦写次数数组（仅 NOR/NAND） */
    if (cfg->type != FLASH_TYPE_EEPROM) {
        uint32_t nblocks = dev->cfg.total_size / dev->cfg.erase_size;
        dev->erase_cycles = (uint32_t *)calloc(nblocks, sizeof(uint32_t));
        if (!dev->erase_cycles) {
            fclose(dev->fp);
            free(dev->image);
            free(dev);
            return NULL;
        }
    }

    return dev;
}

void flash_sim_deinit(flash_dev_t *dev)
{
    if (!dev) { return; }
    if (dev->fp) { fclose(dev->fp); } /* 镜像已在每次操作后落盘，此处确保关闭 */
    if (dev->image) { free(dev->image); }
    if (dev->erase_cycles) { free(dev->erase_cycles); }
    free(dev);
}

flash_err_t flash_sim_read(const flash_dev_t *dev, uint32_t offset,
                           void *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) { return FLASH_ERR_ARGS; }
    if (offset + len > dev->cfg.total_size) { return FLASH_ERR_RANGE; }

    memcpy(buf, dev->image + offset, len);
    ((flash_dev_t *)dev)->stats.total_reads++;
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
        /* 字节级覆盖写，无约束 */
        memcpy(dev->image + offset, src, len);
    } else {
        /* NOR/NAND：目标位须为 1（0xFF），写入只能把 1->0 */
        for (uint32_t i = 0; i < len; i++) {
            if ((dev->image[offset + i] & src[i]) != src[i]) {
                /* 试图把已为 0 的位写回 1，违反 NOR 物理特性 */
                return FLASH_ERR_WRITE;
            }
        }
        for (uint32_t i = 0; i < len; i++) {
            dev->image[offset + i] &= src[i];
        }
    }

    /* 落盘 */
    fseek(dev->fp, (long)offset, SEEK_SET);
    if (fwrite(dev->image + offset, 1, len, dev->fp) != len) {
        return FLASH_ERR_IO;
    }
    fflush(dev->fp);

    dev->stats.total_writes += len;
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

    uint32_t nblocks = dev->cfg.erase_size ? (dev->cfg.total_size / dev->cfg.erase_size) : 0;

    for (uint32_t b = offset; b < offset + len; b += dev->cfg.erase_size) {
        uint32_t blk_idx = b / dev->cfg.erase_size;
        if (blk_idx < nblocks) {
            if (dev->erase_cycles[blk_idx] >= dev->cfg.erase_cycles) {
                /* 寿命耗尽 */
                return FLASH_ERR_ERASE;
            }
            dev->erase_cycles[blk_idx]++;
            if (dev->erase_cycles[blk_idx] > dev->stats.max_erase_cycles) {
                dev->stats.max_erase_cycles = dev->erase_cycles[blk_idx];
            }
        }
        memset(dev->image + b, 0xFF, dev->cfg.erase_size);
        fseek(dev->fp, (long)b, SEEK_SET);
        if (fwrite(dev->image + b, 1, dev->cfg.erase_size, dev->fp)
            != dev->cfg.erase_size) {
            return FLASH_ERR_IO;
        }
        dev->stats.total_erases++;
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
    return FLASH_OK;
}
