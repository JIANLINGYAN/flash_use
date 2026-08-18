/**
 * zms_ad.c - Zephyr ZMS 组件适配器（适配层）
 *
 * 把 Zephyr ZMS（Zephyr Memory Storage，键值存储）封装为应用层统一接口
 * app_component_t，vendor 零修改，经 Zephyr 兼容层桥接模拟基座。
 *
 * ZMS 的 key 是 32 位 ID，适配层把字符串 key hash 映射到 ID 空间。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kvss/zms.h>

#include "app_register.h"
#include "app_util.h"
#include "zephyr_compat.h"

#define ZMS_AD_BIN "app_zms.bin"
#define ZMS_AD_ID_BASE 0x1000u

static flash_dev_t *s_dev = NULL;
static struct zms_fs s_fs;
static int s_inited = 0;

/* key -> 32 位 ID（djb2 变体） */
static zms_id_t zms_ad_key2id(const char *key)
{
    uint32_t h = 5381;
    const unsigned char *p = (const unsigned char *)key;

    while (*p) {
        h = h * 33u + *p;
        p++;
    }
    return (zms_id_t)(ZMS_AD_ID_BASE + h);
}

static int zms_ad_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, ZMS_AD_BIN, FLASH_TYPE_NOR);
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

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        return -1;
    }
    if (!app_env_u32("APP_REINIT", 0)) {
        flash_sim_erase(s_dev, 0, capacity);
    }

    const struct device *zdev =
        zephyr_compat_register_flash(s_dev, cfg.erase_size, 1, 0xFF);
    memset(&s_fs, 0, sizeof(s_fs));
    s_fs.offset = 0;
    s_fs.sector_size = cfg.erase_size;
    s_fs.sector_count = capacity / cfg.erase_size;
    s_fs.flash_device = zdev;

    if (zms_mount(&s_fs) != 0) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    s_inited = 1;
    return 0;
}

static void zms_ad_deinit_impl(void)
{
    s_inited = 0;
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *zms_ad_device_impl(void)
{
    return s_dev;
}

static int zms_ad_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!key || !val || len == 0 || !s_inited) {
        return -1;
    }
    return zms_write(&s_fs, zms_ad_key2id(key), val, len) >= 0 ? 0 : -1;
}

static int zms_ad_get_impl(const char *key, void *val, uint16_t *len)
{
    if (!key || !len || !s_inited) {
        return -1;
    }
    ssize_t rc = zms_read(&s_fs, zms_ad_key2id(key), val, *len);

    if (rc < 0) {
        return -1;
    }
    *len = (uint16_t)rc;
    return 0;
}

static int zms_ad_del_impl(const char *key)
{
    if (!key || !s_inited) {
        return -1;
    }
    return zms_delete(&s_fs, zms_ad_key2id(key)) == 0 ? 0 : -1;
}

static const app_component_t s_zms_comp = {
    .id = "zms",
    .category = "kv",
    .name = "Zephyr ZMS (KV/固定槽位存储)",
    .init = zms_ad_init_impl,
    .deinit = zms_ad_deinit_impl,
    .device = zms_ad_device_impl,
    .kv_set = zms_ad_set_impl,
    .kv_get = zms_ad_get_impl,
    .kv_del = zms_ad_del_impl,
};

static void __attribute__((constructor)) zms_ad_register(void)
{
    app_register(&s_zms_comp);
}
