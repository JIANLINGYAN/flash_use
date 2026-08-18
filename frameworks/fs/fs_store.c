/**
 * fs_store.c - 简易文件系统逻辑框架实现
 *
 * 依赖：flash_sim.h 提供的统一介质接口（read/write/erase）。
 * 适用介质：NOR（块擦除 + 按字节编程）。不适用于 EEPROM（无块擦除）。
 *
 * 布局（区域内）：
 *   [0, block)                    FAT 块（文件分配表）
 *   [block, size)                 数据块区（每块 = 一个擦除块）
 *
 * 块编号约定：FAT 为逻辑块 -1（固定 base 偏移），数据块从 0 开始，
 * 物理偏移 = base + (n+1) * block_size。
 *
 * 覆盖写（fs_write）语义：
 *  - 长度可容纳：擦除数据块后重写（原地复用，无原子保证）。
 *  - 长度超出：先分配新块区间并写新数据，再提交 FAT 指向新区间
 *    （两步提交），旧块成为孤儿供后续分配复用。
 */

#include "fs_store.h"

#include <string.h>
#include <stdlib.h>

/* 文件条目状态 */
#define FS_ENTRY_FREE  0xFFu
#define FS_ENTRY_USED  0x5Au

/* FAT 槽容量（固定 32，块大小 4KB 下足够） */
#define FS_MAX_FILES   32u

/* 文件分配表（单块，落盘结构） */
typedef struct {
    uint8_t  state;                 /* FS_ENTRY_FREE / FS_ENTRY_USED */
    char     name[FS_NAME_MAX];
    uint32_t start_blk;             /* 首数据块索引（0 起） */
    uint32_t blocks;                /* 占用数据块数 */
    uint32_t size;                  /* 文件字节数 */
    uint32_t reserved;
} fs_entry_t;

typedef struct {
    uint32_t magic;                 /* FS_FAT_MAGIC */
    uint32_t seq;                   /* FAT 提交序号（掉电恢复用） */
    fs_entry_t entries[FS_MAX_FILES];
} fs_fat_t;

/* ---------- 运行期上下文 ---------- */
static const flash_hal_t *s_hal = NULL;
static uint32_t s_base = 0;
static uint32_t s_size = 0;
static uint32_t s_block = 0;        /* 块大小 = 擦除块 */
static fs_fat_t s_fat;              /* 内存 FAT 副本 */
static uint32_t s_data_blocks = 0;  /* 数据块总数 */

/* ---------- 块偏移换算 ---------- */
static uint32_t data_blk_off(uint32_t n)
{
    return s_base + (n + 1u) * s_block;
}

/* ---------- 底层读写擦 ---------- */
static int erase_blk(uint32_t off)
{
    return (s_hal->erase(s_hal->ctx, off, s_block) == 0) ? 0 : -1;
}

static int write_blk(uint32_t off, const void *buf, uint32_t len)
{
    return (s_hal->write(s_hal->ctx, off, buf, len) == 0) ? 0 : -1;
}

static int read_blk(uint32_t off, void *buf, uint32_t len)
{
    return (s_hal->read(s_hal->ctx, off, buf, len) == 0) ? 0 : -1;
}

/* ---------- FAT 落盘 ---------- */
static fs_err_t fat_commit(void)
{
    if (erase_blk(s_base) != 0) { return FS_ERR_IO; }
    if (write_blk(s_base, &s_fat, sizeof(s_fat)) != 0) { return FS_ERR_IO; }
    return FS_OK;
}

static fs_err_t fat_load(void)
{
    fs_fat_t raw;
    if (read_blk(s_base, &raw, sizeof(raw)) != 0) { return FS_ERR_IO; }
    if (raw.magic != FS_FAT_MAGIC) {
        /* 无有效 FAT：视为全新介质，返回损坏由调用方决定重建 */
        return FS_ERR_CORRUPT;
    }
    s_fat = raw;
    return FS_OK;
}

/* ---------- 文件条目查找 ---------- */
static fs_entry_t *entry_find(const char *name)
{
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        fs_entry_t *e = &s_fat.entries[i];
        if (e->state == FS_ENTRY_USED && strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

static fs_entry_t *entry_free_slot(void)
{
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (s_fat.entries[i].state != FS_ENTRY_USED) {
            return &s_fat.entries[i];
        }
    }
    return NULL;
}

/* 检查数据块 n 是否被任一已用条目引用 */
static int block_in_use(uint32_t n)
{
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        const fs_entry_t *e = &s_fat.entries[i];
        if (e->state == FS_ENTRY_USED && n >= e->start_blk
            && n < e->start_blk + e->blocks) {
            return 1;
        }
    }
    return 0;
}

/* 首次适配：寻找 need 个连续空闲数据块，返回起始索引或 -1 */
static int find_free_blocks(uint32_t need)
{
    if (need == 0 || need > s_data_blocks) { return -1; }
    for (uint32_t start = 0; start + need <= s_data_blocks; start++) {
        int free = 1;
        for (uint32_t b = start; b < start + need; b++) {
            if (block_in_use(b)) { free = 0; break; }
        }
        if (free) { return (int)start; }
    }
    return -1;
}

/* 擦除并写入一个数据块（len <= s_block） */
static int write_data_blk(uint32_t blk, const void *buf, uint32_t len)
{
    uint32_t off = data_blk_off(blk);
    if (erase_blk(off) != 0) { return -1; }
    if (len && write_blk(off, buf, len) != 0) { return -1; }
    return 0;
}

/* 把数据写到空闲块区间（need 块，首块索引经 out_blk 返回） */
static fs_err_t store_to_blocks(const void *buf, uint32_t len,
                                uint32_t need, uint32_t *out_blk)
{
    int start = find_free_blocks(need);
    if (start < 0) { return FS_ERR_NOSPC; }
    for (uint32_t i = 0; i < need; i++) {
        uint32_t seg = (i == need - 1u)
                       ? (len - (need - 1u) * s_block) : s_block;
        const uint8_t *p = (const uint8_t *)buf + i * s_block;
        if (write_data_blk((uint32_t)start + i, p, seg) != 0) {
            return FS_ERR_IO;
        }
    }
    *out_blk = (uint32_t)start;
    return FS_OK;
}

/* ==================== 公共 API ==================== */

fs_err_t fs_init(const flash_hal_t *hal, uint32_t base, uint32_t size,
                 uint32_t block_size)
{
    if (!hal || size < 2u) { return FS_ERR_ARGS; }
    s_hal = hal;
    s_base = base;
    s_size = size;
    s_block = block_size;
    if (s_block == 0 || s_base % s_block != 0 || s_size % s_block != 0) {
        return FS_ERR_ARGS;
    }
    s_data_blocks = s_size / s_block - 1u;
    if (s_data_blocks == 0) { return FS_ERR_ARGS; }
    memset(&s_fat, 0, sizeof(s_fat));
    return fat_load();   /* 全新介质返回 FS_ERR_CORRUPT */
}

fs_err_t fs_format(const flash_hal_t *hal, uint32_t base, uint32_t size,
                   uint32_t block_size)
{
    if (fs_init(hal, base, size, block_size) != FS_OK) {
        /* 允许 CORRUPT 后重建 */
        if (s_data_blocks == 0) { return FS_ERR_ARGS; }
    }
    for (uint32_t off = base; off < base + size; off += s_block) {
        if (s_hal->erase(s_hal->ctx, off, s_block) != 0) {
            return FS_ERR_IO;
        }
    }
    memset(&s_fat, 0xFF, sizeof(s_fat));
    s_fat.magic = FS_FAT_MAGIC;
    s_fat.seq = 1u;
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        s_fat.entries[i].state = FS_ENTRY_FREE;
    }
    return fat_commit();
}

fs_err_t fs_create(const flash_hal_t *hal, const char *name)
{
    if (!hal || !name || !*name || strlen(name) >= FS_NAME_MAX) {
        return FS_ERR_ARGS;
    }
    if (entry_find(name)) { return FS_ERR_EXIST; }
    fs_entry_t *e = entry_free_slot();
    if (!e) { return FS_ERR_NOSPC; }
    memset(e, 0, sizeof(*e));
    e->state = FS_ENTRY_USED;
    strncpy(e->name, name, FS_NAME_MAX - 1u);
    e->size = 0;
    return fat_commit();
}

fs_err_t fs_delete(const flash_hal_t *hal, const char *name)
{
    if (!hal || !name) { return FS_ERR_ARGS; }
    fs_entry_t *e = entry_find(name);
    if (!e) { return FS_ERR_NOTFOUND; }
    memset(e, 0, sizeof(*e));
    e->state = FS_ENTRY_FREE;
    return fat_commit();
}

fs_err_t fs_write(const flash_hal_t *hal, const char *name,
                  const void *buf, uint32_t len)
{
    if (!hal || !name || !*name || strlen(name) >= FS_NAME_MAX) {
        return FS_ERR_ARGS;
    }
    fs_entry_t *e = entry_find(name);
    if (!e) {
        fs_err_t rc = fs_create(hal, name);
        if (rc != FS_OK) { return rc; }
        e = entry_find(name);
    }
    if (len == 0) {
        e->size = 0;
        return fat_commit();
    }

    uint32_t need = (len + s_block - 1u) / s_block;
    if (need <= e->blocks && e->blocks > 0) {
        /* 原地复用：按 need 逐块擦写（收缩时只写 need 块，尾部保持 0xFF） */
        for (uint32_t i = 0; i < need; i++) {
            uint32_t seg = (i == need - 1u)
                           ? (len - (need - 1u) * s_block) : s_block;
            const uint8_t *p = (const uint8_t *)buf + i * s_block;
            if (write_data_blk(e->start_blk + i, p, seg) != 0) {
                return FS_ERR_IO;
            }
        }
        e->size = len;
        return fat_commit();
    }

    /* 需要扩展：两步提交迁移到新块区间 */
    uint32_t new_blk = 0;
    fs_err_t rc = store_to_blocks(buf, len, need, &new_blk);
    if (rc != FS_OK) { return rc; }
    e->start_blk = new_blk;
    e->blocks = need;
    e->size = len;
    return fat_commit();
}

fs_err_t fs_append(const flash_hal_t *hal, const char *name,
                   const void *buf, uint32_t len)
{
    if (!hal || !name || !*name) { return FS_ERR_ARGS; }
    fs_entry_t *e = entry_find(name);
    if (!e) { return FS_ERR_NOTFOUND; }

    /* 优先利用文件末尾数据块的擦除态空闲区（1->0 编程追加） */
    uint32_t in_blk = e->size % s_block;
    if (e->blocks > 0 && e->size > 0 && in_blk + len <= s_block) {
        uint32_t off = data_blk_off(e->start_blk + e->blocks - 1u) + in_blk;
        if (s_hal->write(s_hal->ctx, off, buf, len) == 0) {
            e->size += len;
            return fat_commit();
        }
    }
    /* 尾部不可追加：读旧数据拼接后整体重写 */
    uint8_t *tmp = (uint8_t *)malloc(e->size + len);
    if (!tmp) { return FS_ERR_NOSPC; }
    uint32_t old = e->size;
    fs_err_t rc = FS_OK;
    if (old) {
        uint32_t rd = old;
        rc = fs_read(hal, name, tmp, 0, &rd);
        if (rc != FS_OK || rd != old) { rc = FS_ERR_IO; }
    }
    if (rc == FS_OK) {
        memcpy(tmp + old, buf, len);
        rc = fs_write(hal, name, tmp, old + len);
    }
    free(tmp);
    return rc;
}

fs_err_t fs_read(const flash_hal_t *hal, const char *name,
                 void *buf, uint32_t offset, uint32_t *len)
{
    if (!hal || !name || !buf || !len) { return FS_ERR_ARGS; }
    fs_entry_t *e = entry_find(name);
    if (!e) { return FS_ERR_NOTFOUND; }
    if (offset >= e->size) {
        *len = 0;
        return FS_OK;
    }
    uint32_t avail = e->size - offset;
    if (*len > avail) { *len = avail; }
    uint32_t blk_off = offset % s_block;
    uint32_t done = 0;
    while (done < *len) {
        uint32_t blk = e->start_blk + (offset + done) / s_block;
        uint32_t n = *len - done;
        if (n > s_block - blk_off) { n = s_block - blk_off; }
        uint32_t off = data_blk_off(blk) + blk_off;
        if (read_blk(off, (uint8_t *)buf + done, n) != 0) { return FS_ERR_IO; }
        done += n;
        blk_off = 0;
    }
    return FS_OK;
}

fs_err_t fs_get_size(const flash_hal_t *hal, const char *name, uint32_t *size)
{
    if (!hal || !name || !size) { return FS_ERR_ARGS; }
    fs_entry_t *e = entry_find(name);
    if (!e) { return FS_ERR_NOTFOUND; }
    *size = e->size;
    return FS_OK;
}

bool fs_exists(const flash_hal_t *hal, const char *name)
{
    if (!hal || !name) { return false; }
    return entry_find(name) != NULL;
}

uint32_t fs_file_count(const flash_hal_t *hal)
{
    (void)hal;
    uint32_t n = 0;
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (s_fat.entries[i].state == FS_ENTRY_USED) { n++; }
    }
    return n;
}

uint32_t fs_block_size(const flash_hal_t *hal)
{
    (void)hal;
    return s_block;
}
