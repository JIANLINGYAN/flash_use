/**
 * kv_store_ad.c - 自研 KV 框架适配器（适配层：kv_store -> app_component_t）
 *
 * 本文件把 frameworks/kv/kv_store 的 API 封装为应用层统一接口
 * （app_component_t）。任务引擎（app_task.c）通过本适配器调用组件层，
 * 因此组件本身的源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_register.h"
#include "app_util.h"
#include "kv_store.h"

#define KV_AD_BIN "app_kv_store.bin"

static flash_dev_t *s_dev = NULL;
static uint32_t s_capacity = 0;

static int kv_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, KV_AD_BIN, FLASH_TYPE_NOR);
    if (cfg.type == FLASH_TYPE_EEPROM) {
        /* 自研 KV 需要块擦除语义，EEPROM 自动切换 NOR */
        cfg.type = FLASH_TYPE_NOR;
    }
    s_capacity = opt->capacity;
    if (s_capacity == 0) {
        s_capacity = cfg.total_size >= 8192 ? 8192 : cfg.total_size;
    }
    s_capacity -= s_capacity % cfg.erase_size;
    if (s_capacity < cfg.erase_size) {
        s_capacity = cfg.erase_size;
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        return -1;
    }
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，仅重新扫描加载 */
    if (!app_env_u32("APP_REINIT", 0)) {
        flash_sim_erase(s_dev, 0, s_capacity);
    }
    return kv_init(s_dev, 0, s_capacity) == FLASH_OK ? 0 : -1;
}

static void kv_deinit_impl(void)
{
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *kv_device_impl(void)
{
    return s_dev;
}

/* 字符串 key -> uint16 key_id（适配层约定 key 形如 "k0".."k65535"，
 * 或裸数字 "0".."65535"）。kv_store 约定 key_id=0 保留，统一 +1 映射。 */
static uint16_t kv_key_id(const char *key)
{
    if (!key) {
        return 0;
    }
    const char *dig = key;
    while (*dig && !(*dig >= '0' && *dig <= '9')) {
        dig++;
    }
    long v = *dig ? strtol(dig, NULL, 10) : 0;
    if (v < 0) {
        v = 0;
    }
    return (uint16_t)(v + 1);
}

static int kv_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!s_dev || !val || len == 0) {
        return -1;
    }
    return kv_write(s_dev, kv_key_id(key), val, len) == FLASH_OK ? 0 : -1;
}

static int kv_get_impl(const char *key, void *val, uint16_t *len)
{
    if (!s_dev || !len) {
        return -1;
    }
    return kv_read(s_dev, kv_key_id(key), val, len) == FLASH_OK ? 0 : -1;
}

static int kv_del_impl(const char *key)
{
    if (!s_dev) {
        return -1;
    }
    return kv_delete(s_dev, kv_key_id(key)) == FLASH_OK ? 0 : -1;
}

static const app_component_t s_kv_store_comp = {
    .id = "kv",
    .category = "kv",
    .name = "自研 KV/NVS (kv_store)",
    .init = kv_init_impl,
    .deinit = kv_deinit_impl,
    .device = kv_device_impl,
    .kv_set = kv_set_impl,
    .kv_get = kv_get_impl,
    .kv_del = kv_del_impl,
};

static void __attribute__((constructor)) kv_store_ad_register(void)
{
    app_register(&s_kv_store_comp);
}
