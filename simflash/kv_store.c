/**
 * kv_store.c - 简易 KV 存储逻辑框架实现
 *
 * 依赖：flash_sim.h 提供的统一介质接口（read/write/erase）。
 * 存储介质：NOR Flash 或 EEPROM 均可（EEPROM 无需 erase，逻辑自动适配）。
 *
 * 掉电安全模型：每条记录末尾一个状态字节，先写数据(PENDING)再写状态
 * (COMMITTED)。加载/读取时仅承认状态 == COMMITTED 且 CRC 校验通过的记录。
 *
 * 垃圾回收（compact）：当写入游标到达区域末尾且存在被覆盖/删除产生的
 * 失效记录时，整体擦除 KV 区并把当前有效记录重写到头部，实现空间回收，
 * 同时是磨损均衡的极简形式。
 */

#include "kv_store.h"

#include <string.h>

/* ---------- 软件 CRC32（IEEE 802.3，查表法） ---------- */
static uint32_t s_crc_table[256];
static int s_crc_ready = 0;

static void crc32_init_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crc_table[i] = c;
    }
    s_crc_ready = 1;
}

static uint32_t crc32_calc(const uint8_t *buf, uint32_t len)
{
    if (!s_crc_ready) { crc32_init_table(); }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        c = s_crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* ---------- 运行期上下文 ---------- */
static flash_dev_t *s_dev = NULL;
static uint32_t s_base = 0;
static uint32_t s_size = 0;
static uint32_t s_write_cursor = 0;   /* 下一笔可写偏移（相对 base） */
static uint32_t s_used_bytes = 0;     /* 累计写入字节（含历史失效） */

/* 记录头部 + 状态字节 的总长度（不含 value） */
#define KV_REC_OVERHEAD (sizeof(kv_header_t) + 1u)

/* 单条记录占用的总字节 = 头部 + value + 状态字节 */
static uint32_t rec_total(uint16_t len)
{
    return KV_REC_OVERHEAD + len;
}

/* 读取某条记录（起始 off、value 长度 len）末尾的状态字节 */
static flash_err_t read_state(uint32_t off, uint16_t len, uint8_t *st)
{
    uint32_t state_off = s_base + off + sizeof(kv_header_t) + (uint32_t)len;
    return flash_sim_read(s_dev, state_off, st, 1);
}

/* 扫描整个 KV 区域，重建索引与写入游标（掉电安全加载） */
static flash_err_t scan_region(void)
{
    s_write_cursor = 0;
    uint32_t off = 0;
    while (off + KV_REC_OVERHEAD <= s_size) {
        kv_header_t hdr;
        flash_err_t rc = flash_sim_read(s_dev, s_base + off, &hdr, sizeof(hdr));
        if (rc != FLASH_OK) { return rc; }

        if (hdr.magic != KV_MAGIC) {
            /* 非记录起始：通常意味着已到空白区（0xFF），停止扫描 */
            break;
        }

        uint8_t st = KV_STATE_ERASED;
        rc = read_state(off, hdr.len, &st);
        if (rc != FLASH_OK) { return rc; }

        if (st != KV_STATE_COMMITTED) {
            /* PENDING 或 ERASED：掉电中断的残记录，跳过且不视为有效 */
            /* 但游标仍前进，避免覆盖半截记录 */
            uint32_t total = rec_total(hdr.len);
            if (off + total > s_size) { break; }
            off += total;
            s_write_cursor = off;
            s_used_bytes += total;
            continue;
        }

        /* 已提交记录：校验 CRC */
        uint32_t total = rec_total(hdr.len);
        if (off + total > s_size) { break; }
        if (hdr.len > KV_MAX_VALUE) {
            off += total;
            s_write_cursor = off;
            s_used_bytes += total;
            continue;
        }
        uint8_t val[KV_MAX_VALUE];
        rc = flash_sim_read(s_dev, s_base + off + sizeof(kv_header_t), val, hdr.len);
        if (rc != FLASH_OK) { return rc; }
        uint32_t crc = crc32_calc(val, hdr.len);
        if (crc == hdr.crc) {
            /* 有效记录：索引由"后写覆盖"自然形成——读取时总是取最后一次 */
            /* 此处不维护哈希表，读取阶段再次线性定位最后一条有效记录 */
        }
        off += total;
        s_write_cursor = off;
        s_used_bytes += total;
    }
    return FLASH_OK;
}

/* 压实：擦除整个 KV 区，把当前有效记录重写到头部（演示 GC + 磨损均衡） */
static flash_err_t compact(void)
{
    /* 先收集当前有效记录（key -> 最新值） */
    /* 简化实现：重新扫描，得到有效集（用 key 数组保留最新） */
    typedef struct { uint16_t key; uint16_t len; uint8_t val[KV_MAX_VALUE]; } entry_t;
    entry_t valid[64];
    uint32_t nvalid = 0;

    uint32_t off = 0;
    while (off + KV_REC_OVERHEAD <= s_size) {
        kv_header_t hdr;
        if (flash_sim_read(s_dev, s_base + off, &hdr, sizeof(hdr)) != FLASH_OK) break;
        if (hdr.magic != KV_MAGIC) break;
        uint8_t st = KV_STATE_ERASED;
        if (read_state(off, hdr.len, &st) != FLASH_OK) break;
        if (st != KV_STATE_COMMITTED) { off += rec_total(hdr.len); continue; }
        if (hdr.len > KV_MAX_VALUE) { off += rec_total(hdr.len); continue; }
        uint8_t val[KV_MAX_VALUE];
        uint32_t crc;
        if (hdr.len == 0) {
            crc = crc32_calc(val, 0);
        } else {
            if (flash_sim_read(s_dev, s_base + off + sizeof(hdr), val, hdr.len) != FLASH_OK) break;
            crc = crc32_calc(val, hdr.len);
        }
        if (crc != hdr.crc) { off += rec_total(hdr.len); continue; }

        /* 用最新值覆盖同名 key */
        uint32_t i;
        for (i = 0; i < nvalid; i++) {
            if (valid[i].key == hdr.key_id) { break; }
        }
        if (i == nvalid && nvalid < 64) { nvalid++; }
        if (i < nvalid) {
            valid[i].key = hdr.key_id;
            valid[i].len = hdr.len;
            memcpy(valid[i].val, val, hdr.len);
        }
        off += rec_total(hdr.len);
    }

    /* 擦除整个 KV 区（要求 base/size 块对齐） */
    flash_err_t rc = flash_sim_erase(s_dev, s_base, s_size);
    if (rc != FLASH_OK && rc != FLASH_ERR_NOTSUP) { return rc; }

    /* 重写有效记录（EEPROM 无需擦除，直接覆盖写，此处 erase 被忽略） */
    s_write_cursor = 0;
    s_used_bytes = 0;
    for (uint32_t i = 0; i < nvalid; i++) {
        rc = kv_write(s_dev, valid[i].key, valid[i].val, valid[i].len);
        if (rc != FLASH_OK) { return rc; }
    }
    return FLASH_OK;
}

flash_err_t kv_init(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    if (!dev || size == 0) { return FLASH_ERR_ARGS; }
    s_dev = dev;
    s_base = base;
    s_size = size;
    s_used_bytes = 0;
    return scan_region();
}

flash_err_t kv_write(flash_dev_t *dev, uint16_t key_id,
                     const void *value, uint16_t len)
{
    if (!dev || dev != s_dev) { return FLASH_ERR_ARGS; }
    if (key_id == 0 || len > KV_MAX_VALUE) { return FLASH_ERR_ARGS; }

    uint32_t total = rec_total(len);

    /* 空间不足：尝试压实回收，再判一次 */
    if (s_write_cursor + total > s_size) {
        flash_err_t rc = compact();
        if (rc != FLASH_OK) { return rc; }
        if (s_write_cursor + total > s_size) { return FLASH_ERR_RANGE; }
    }

    uint32_t off = s_write_cursor;
    kv_header_t hdr;
    hdr.magic = KV_MAGIC;
    hdr.key_id = key_id;
    hdr.len = len;
    hdr.crc = crc32_calc((const uint8_t *)value, len);

    /* 第一步：写头部 + value（状态字节保持 ERASED=0xFF，即 PENDING 表象） */
    flash_err_t rc = flash_sim_write(s_dev, s_base + off, &hdr, sizeof(hdr));
    if (rc != FLASH_OK) { return rc; }
    if (len > 0) {
        rc = flash_sim_write(s_dev, s_base + off + sizeof(hdr), value, len);
        if (rc != FLASH_OK) { return rc; }
    }

    /* 第二步：写状态字节为 COMMITTED（原子生效点） */
    uint8_t committed = KV_STATE_COMMITTED;
    rc = flash_sim_write(s_dev, s_base + off + sizeof(hdr) + len, &committed, 1);
    if (rc != FLASH_OK) { return rc; }

    s_write_cursor += total;
    s_used_bytes += total;
    return FLASH_OK;
}

flash_err_t kv_read(flash_dev_t *dev, uint16_t key_id,
                    void *value, uint16_t *len)
{
    if (!dev || dev != s_dev || !len) { return FLASH_ERR_ARGS; }

    /* 线性扫描，取最后一条 COMMITTED 且 CRC 通过的同名记录 */
    uint8_t *out = (uint8_t *)value;
    uint32_t off = 0;
    int found = 0;
    uint16_t found_len = 0;
    uint8_t found_val[KV_MAX_VALUE];

    while (off + KV_REC_OVERHEAD <= s_size) {
        kv_header_t hdr;
        if (flash_sim_read(s_dev, s_base + off, &hdr, sizeof(hdr)) != FLASH_OK) break;
        if (hdr.magic != KV_MAGIC) break;
        uint8_t st = KV_STATE_ERASED;
        if (read_state(off, hdr.len, &st) != FLASH_OK) break;
        if (st != KV_STATE_COMMITTED) { off += rec_total(hdr.len); continue; }
        if (hdr.key_id != key_id) { off += rec_total(hdr.len); continue; }
        if (hdr.len > KV_MAX_VALUE) { off += rec_total(hdr.len); continue; }
        uint8_t val[KV_MAX_VALUE];
        uint32_t crc_calc;
        if (hdr.len == 0) {
            crc_calc = crc32_calc(val, 0); /* 空值 CRC，与写入端一致 */
        } else {
            if (flash_sim_read(s_dev, s_base + off + sizeof(hdr), val, hdr.len) != FLASH_OK) break;
            crc_calc = crc32_calc(val, hdr.len);
        }
        if (crc_calc != hdr.crc) { off += rec_total(hdr.len); continue; }

        /* 命中且合法：记录为候选（后续同名记录会继续覆盖） */
        if (hdr.len == 0) {
            /* len=0 的 COMMITTED 记录表示"已删除"，视为未找到 */
            found = 0;
        } else {
            found = 1;
            found_len = hdr.len;
            memcpy(found_val, val, hdr.len);
        }
        off += rec_total(hdr.len);
    }

    if (!found) { return FLASH_ERR_ARGS; }

    if (out != NULL) {
        if (*len < found_len) { return FLASH_ERR_RANGE; }
        memcpy(out, found_val, found_len);
    }
    *len = found_len;
    return FLASH_OK;
}

flash_err_t kv_delete(flash_dev_t *dev, uint16_t key_id)
{
    /* 写一条 len=0 的 COMMITTED 记录，使后续读取取到空值即"已删除" */
    return kv_write(dev, key_id, NULL, 0);
}

uint32_t kv_used_bytes(void)
{
    return s_used_bytes;
}

flash_err_t kv_summary(flash_dev_t *dev, kv_summary_t *sum)
{
    if (!dev || dev != s_dev || !sum) { return FLASH_ERR_ARGS; }
    sum->valid_entries = 0;
    sum->total_records = 0;

    uint8_t seen_key[256] = {0};
    uint32_t off = 0;
    while (off + KV_REC_OVERHEAD <= s_size) {
        kv_header_t hdr;
        if (flash_sim_read(s_dev, s_base + off, &hdr, sizeof(hdr)) != FLASH_OK) break;
        if (hdr.magic != KV_MAGIC) break;
        uint8_t st = KV_STATE_ERASED;
        if (read_state(off, hdr.len, &st) != FLASH_OK) break;
        if (st != KV_STATE_COMMITTED) { off += rec_total(hdr.len); continue; }
        if (hdr.len > KV_MAX_VALUE) { off += rec_total(hdr.len); continue; }
        uint8_t val[KV_MAX_VALUE];
        uint32_t crc_calc;
        if (hdr.len == 0) {
            crc_calc = crc32_calc(val, 0); /* 空值 CRC，与写入端一致 */
        } else {
            if (flash_sim_read(s_dev, s_base + off + sizeof(hdr), val, hdr.len) != FLASH_OK) break;
            crc_calc = crc32_calc(val, hdr.len);
        }
        if (crc_calc != hdr.crc) { off += rec_total(hdr.len); continue; }

        sum->total_records++;
        if (hdr.key_id < 256 && !seen_key[hdr.key_id]) {
            seen_key[hdr.key_id] = 1;
            if (hdr.len > 0) { sum->valid_entries++; } /* len=0 视为删除 */
        }
        off += rec_total(hdr.len);
    }
    return FLASH_OK;
}
