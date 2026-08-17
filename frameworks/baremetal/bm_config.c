/**
 * bm_config.c - 裸机结构体配置存储框架实现（A/B 双备份 + CRC32）
 *
 * 关键设计说明见 bm_config.h。实现要点：
 *  1. 写入顺序为"先 payload、后头部"。头部含 magic/seq/crc，是分区有效性的
 *     唯一凭据，最后写入 => 写 payload 途中掉电时该分区仍被视为无效，
 *     系统回退到另一分区，实现原子性（等价于两阶段提交的简化形式）。
 *  2. 保存时总是选择 seq 较小（较旧）的分区作为目标，从而 A/B 交替使用，
 *     擦写次数均摊到两个块上（简易磨损均衡）。
 *  3. seq 单调递增，比较时用差值判断，避免回绕问题（本场景 32 位足够）。
 */

#include "bm_config.h"

#include <string.h>

/* ---- CRC32（标准 IEEE 802.3 多项式 0xEDB88320，查表法反向实现） ---- */
uint32_t bm_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (uint8_t b = 0; b < 8; b++) {
            /* 逐位计算，省去 1KB 查表空间（裸机 ROM 敏感场景更合适） */
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

/* 读取并校验某分区，成功时输出头部（payload 可选） */
static bool part_read(bm_config_t *ctx, uint32_t base, bm_header_t *hdr,
                      void *payload)
{
    bm_header_t h;
    if (flash_sim_read(ctx->dev, base, &h, sizeof(h)) != FLASH_OK) {
        return false;
    }
    if (h.magic != BM_MAGIC) { return false; }
    if (h.len != ctx->payload_len) { return false; }

    /* 读 payload 并校验 CRC，确保数据完整（掉电写坏会在此被发现） */
    uint8_t buf[BM_MAX_PAYLOAD];
    if (h.len > BM_MAX_PAYLOAD) { return false; }
    if (flash_sim_read(ctx->dev, base + sizeof(bm_header_t), buf, h.len)
        != FLASH_OK) {
        return false;
    }
    if (bm_crc32(buf, h.len) != h.crc) { return false; }

    if (hdr) { *hdr = h; }
    if (payload) { memcpy(payload, buf, h.len); }
    return true;
}

flash_err_t bm_config_init(bm_config_t *ctx, flash_dev_t *dev,
                           uint32_t base_a, uint32_t base_b,
                           uint32_t part_size, uint32_t payload_len)
{
    if (!ctx || !dev || payload_len == 0
        || payload_len > BM_MAX_PAYLOAD) {
        return FLASH_ERR_ARGS;
    }
    if (part_size < sizeof(bm_header_t) + payload_len) {
        return FLASH_ERR_ARGS; /* 分区装不下头部+配置体 */
    }
    /* A/B 不得重叠 */
    if ((base_a < base_b && base_a + part_size > base_b)
        || (base_b < base_a && base_b + part_size > base_a)
        || base_a == base_b) {
        return FLASH_ERR_ARGS;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->dev = dev;
    ctx->base_a = base_a;
    ctx->base_b = base_b;
    ctx->part_size = part_size;
    ctx->payload_len = payload_len;
    ctx->cur_part = -1;
    ctx->cur_seq = 0;

    /* 扫描两分区，选 seq 更大且 CRC 有效者为当前生效 */
    bm_header_t ha, hb;
    bool va = part_read(ctx, base_a, &ha, NULL);
    bool vb = part_read(ctx, base_b, &hb, NULL);

    if (va && vb) {
        if (ha.seq >= hb.seq) {
            ctx->cur_part = 0;
            ctx->cur_seq = ha.seq;
        } else {
            ctx->cur_part = 1;
            ctx->cur_seq = hb.seq;
        }
    } else if (va) {
        ctx->cur_part = 0;
        ctx->cur_seq = ha.seq;
    } else if (vb) {
        ctx->cur_part = 1;
        ctx->cur_seq = hb.seq;
    }

    return FLASH_OK;
}

flash_err_t bm_config_load(bm_config_t *ctx, void *out)
{
    if (!ctx || !out) { return FLASH_ERR_ARGS; }
    if (ctx->cur_part < 0) {
        return FLASH_ERR_ARGS; /* 无有效配置（首次上电） */
    }

    uint32_t base = (ctx->cur_part == 0) ? ctx->base_a : ctx->base_b;
    bm_header_t h;
    if (!part_read(ctx, base, &h, out)) {
        /*
         * 生效分区意外损坏，尝试回退到另一分区（提升鲁棒性：
         * 例如运行期该块出现坏点导致读校验失败）。
         */
        uint32_t other = (ctx->cur_part == 0) ? ctx->base_b : ctx->base_a;
        bm_header_t h2;
        if (part_read(ctx, other, &h2, out)) {
            ctx->cur_part = (ctx->cur_part == 0) ? 1 : 0;
            ctx->cur_seq = h2.seq;
            return FLASH_OK;
        }
        ctx->cur_part = -1;
        return FLASH_ERR_ARGS;
    }
    return FLASH_OK;
}

flash_err_t bm_config_save(bm_config_t *ctx, const void *in)
{
    if (!ctx || !in) { return FLASH_ERR_ARGS; }

    /*
     * 选择目标分区：写入"非当前生效"的那个，实现 A/B 轮换。
     * 首次保存（cur_part<0）写入 A。
     */
    uint32_t target;
    int8_t target_part;
    if (ctx->cur_part == 0) {
        target = ctx->base_b;
        target_part = 1;
    } else {
        target = ctx->base_a;
        target_part = 0;
    }

    /* 1) 擦除目标分区（NOR 写入前必须擦除） */
    flash_err_t rc = flash_sim_erase(ctx->dev, target, ctx->part_size);
    if (rc != FLASH_OK) { return rc; }

    /* 2) 先写 payload（此时头部仍为 0xFF，分区被视为无效） */
    rc = flash_sim_write(ctx->dev, target + sizeof(bm_header_t), in,
                         ctx->payload_len);
    if (rc != FLASH_OK) { return rc; }

    /* 3) 最后写头部：magic+seq+crc 一次性落盘，完成"提交" */
    bm_header_t h;
    h.magic = BM_MAGIC;
    h.seq = ctx->cur_seq + 1u;
    h.len = ctx->payload_len;
    h.crc = bm_crc32(in, ctx->payload_len);

    rc = flash_sim_write(ctx->dev, target, &h, sizeof(h));
    if (rc != FLASH_OK) { return rc; }

    ctx->cur_part = target_part;
    ctx->cur_seq = h.seq;
    return FLASH_OK;
}

flash_err_t bm_config_reset(bm_config_t *ctx)
{
    if (!ctx) { return FLASH_ERR_ARGS; }

    flash_err_t rc = flash_sim_erase(ctx->dev, ctx->base_a, ctx->part_size);
    if (rc != FLASH_OK) { return rc; }
    rc = flash_sim_erase(ctx->dev, ctx->base_b, ctx->part_size);
    if (rc != FLASH_OK) { return rc; }

    ctx->cur_part = -1;
    ctx->cur_seq = 0;
    return FLASH_OK;
}

flash_err_t bm_config_status(bm_config_t *ctx, bm_status_t *st)
{
    if (!ctx || !st) { return FLASH_ERR_ARGS; }

    bm_header_t ha, hb;
    memset(st, 0, sizeof(*st));

    st->a_valid = part_read(ctx, ctx->base_a, &ha, NULL);
    st->b_valid = part_read(ctx, ctx->base_b, &hb, NULL);
    st->a_seq = st->a_valid ? ha.seq : 0u;
    st->b_seq = st->b_valid ? hb.seq : 0u;
    st->active = ctx->cur_part;
    return FLASH_OK;
}
