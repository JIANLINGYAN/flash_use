/**
 * fcb_ad.c - Zephyr FCB 组件适配器（适配层）
 *
 * 把 Zephyr FCB（闪存环形缓冲：append-only 日志 + 回卷覆盖）封装为应用层
 * 统一接口 app_component_t，vendor 零修改，经 Zephyr 兼容层桥接模拟基座。
 *
 * FCB 本身无 key-value 语义（只有顺序记录），适配层在记录载荷前置
 * "key_len + key" 头，实现 KV 语义：
 *   - kv_set：追加一条 {key_len,key,data} 记录；
 *   - kv_get：遍历整环，取 key 匹配的"最新"记录（后写覆盖）；
 *   - kv_del：追加一条 key 匹配、data 长度 0 的墓碑记录（get 视为已删除）。
 * 适合应用层做 KV 对比（注意 FCB 的写放大特性）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fcb.h>

#include "app_register.h"
#include "app_util.h"
#include "zephyr_compat.h"

#define FCB_AD_BIN "app_fcb.bin"
#define FCB_AD_MAGIC 0x0FCB2026
#define FCB_AD_KEYMAX 31
#define FCB_AD_SECTORS 16

static flash_dev_t *s_dev = NULL;
static struct fcb s_fcb;
static struct flash_sector s_sectors[FCB_AD_SECTORS];
static int s_inited = 0;

/* 载荷布局：key_len(1B) | key | data */
static int fcb_ad_append(const char *key, const void *val, uint16_t len)
{
    uint8_t buf[1 + FCB_AD_KEYMAX + 4096];
    size_t klen = strlen(key);
    struct fcb_entry loc;
    int rc;

    if (klen == 0 || klen > FCB_AD_KEYMAX) {
        return -1;
    }
    if (1u + klen + len > sizeof(buf)) {
        return -1;
    }
    buf[0] = (uint8_t)klen;
    memcpy(buf + 1, key, klen);
    if (val && len) {
        memcpy(buf + 1 + klen, val, len);
    }

    rc = fcb_append(&s_fcb, (uint16_t)(1u + klen + len), &loc);
    if (rc) {
        return rc;
    }
    rc = flash_area_write(s_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), buf,
                          1u + klen + len);
    if (rc) {
        return rc;
    }
    return fcb_append_finish(&s_fcb, &loc);
}

/* 遍历找 key 的最新有效记录；*out_data_off/*out_len 输出载荷数据位置 */
static int fcb_ad_find(const char *key, uint32_t *out_data_off, uint32_t *out_len)
{
    struct fcb_entry loc;
    int rc;
    size_t klen = strlen(key);
    int found = 0;
    uint32_t found_off = 0, found_len = 0;

    if (klen == 0 || klen > FCB_AD_KEYMAX) {
        return -1;
    }
    memset(&loc, 0, sizeof(loc));
    while ((rc = fcb_getnext(&s_fcb, &loc)) == 0) {
        uint8_t hdr[1 + FCB_AD_KEYMAX];
        size_t hlen = 1u + klen;

        if (loc.fe_data_len < hlen) {
            continue;
        }
        if (flash_area_read(s_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), hdr,
                            hlen) != 0) {
            continue;
        }
        if (hdr[0] != klen || memcmp(hdr + 1, key, klen) != 0) {
            continue;
        }
        /* 命中：记录位置（后写覆盖，保留最后命中） */
        found = 1;
        found_off = (uint32_t)(FCB_ENTRY_FA_DATA_OFF(loc) + hlen);
        found_len = loc.fe_data_len - (uint16_t)hlen;
    }
    if (!found) {
        return -1;
    }
    /* len==0 表示墓碑（已删除） */
    if (found_len == 0) {
        return -1;
    }
    *out_data_off = found_off;
    *out_len = found_len;
    return 0;
}

static int fcb_ad_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, FCB_AD_BIN, FLASH_TYPE_NOR);
    if (cfg.type == FLASH_TYPE_EEPROM) {
        cfg.type = FLASH_TYPE_NOR;
    }
    if (cfg.write_size != 1) {
        cfg.write_size = 1;
    }

    uint32_t capacity = opt->capacity;
    if (capacity == 0) {
        capacity = cfg.total_size >= 16384 ? 16384 : cfg.total_size;
    }
    capacity -= capacity % cfg.erase_size;
    if (capacity < cfg.erase_size * 2u) {
        capacity = cfg.erase_size * 2u;
    }
    if (capacity > cfg.total_size) {
        capacity = cfg.total_size - (cfg.total_size % cfg.erase_size);
    }
    uint32_t nsectors = capacity / cfg.erase_size;
    if (nsectors > FCB_AD_SECTORS) {
        nsectors = FCB_AD_SECTORS;
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        return -1;
    }
    if (!app_env_u32("APP_REINIT", 0)) {
        flash_sim_erase(s_dev, 0, nsectors * cfg.erase_size);
    }

    const struct device *zdev =
        zephyr_compat_register_flash(s_dev, cfg.erase_size, 1, 0xFF);
    zephyr_compat_register_area(0, zdev, 0, (off_t)nsectors * cfg.erase_size);

    memset(&s_fcb, 0, sizeof(s_fcb));
    s_fcb.f_magic = FCB_AD_MAGIC;
    s_fcb.f_version = 1;
    s_fcb.f_sector_cnt = (uint16_t)nsectors;
    s_fcb.f_scratch_cnt = 1;
    for (uint32_t i = 0; i < nsectors; i++) {
        s_sectors[i].fs_off = (off_t)i * cfg.erase_size;
        s_sectors[i].fs_size = cfg.erase_size;
    }
    s_fcb.f_sectors = s_sectors;

    if (fcb_init(0, &s_fcb) != 0) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    s_inited = 1;
    return 0;
}

static void fcb_ad_deinit_impl(void)
{
    s_inited = 0;
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *fcb_ad_device_impl(void)
{
    return s_dev;
}

static int fcb_ad_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!key || !val || len == 0 || !s_inited) {
        return -1;
    }
    return fcb_ad_append(key, val, len) == 0 ? 0 : -1;
}

static int fcb_ad_get_impl(const char *key, void *val, uint16_t *len)
{
    uint32_t data_off = 0, dlen = 0;

    if (!key || !len || !s_inited) {
        return -1;
    }
    if (fcb_ad_find(key, &data_off, &dlen) != 0) {
        return -1;
    }
    if (dlen > *len) {
        return -1;
    }
    if (flash_area_read(s_fcb.fap, data_off, val, dlen) != 0) {
        return -1;
    }
    *len = (uint16_t)dlen;
    return 0;
}

static int fcb_ad_del_impl(const char *key)
{
    if (!key || !s_inited) {
        return -1;
    }
    /* 追加墓碑：key 匹配 + 空载荷 */
    return fcb_ad_append(key, NULL, 0) == 0 ? 0 : -1;
}

static const app_component_t s_fcb_comp = {
    .id = "fcb",
    .category = "kv",
    .name = "Zephyr FCB (Flash 环形缓冲/KV)",
    .init = fcb_ad_init_impl,
    .deinit = fcb_ad_deinit_impl,
    .device = fcb_ad_device_impl,
    .kv_set = fcb_ad_set_impl,
    .kv_get = fcb_ad_get_impl,
    .kv_del = fcb_ad_del_impl,
};

static void __attribute__((constructor)) fcb_ad_register(void)
{
    app_register(&s_fcb_comp);
}
