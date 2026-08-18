/**
 * flashdb_ad.c - FlashDB 组件适配器（适配层）
 *
 * 把开源 KV 组件 FlashDB（armink/FlashDB KVDB + FAL）封装为应用层
 * 统一接口 app_component_t，组件源码零修改，仅经移植层
 * （fal_flash_sim_port.c）对接模拟基座。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fal.h>
#include <flashdb.h>

#include "app_register.h"
#include "app_util.h"
#include "fal_flash_sim_port.h"

#define FDB_AD_BIN "app_flashdb.bin"

static flash_dev_t *s_dev = NULL;
static uint32_t s_capacity = 0;
static struct fdb_kvdb s_kvdb;
static int s_kvdb_inited = 0;

static int fdb_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, FDB_AD_BIN, FLASH_TYPE_NOR);
    if (cfg.type == FLASH_TYPE_EEPROM) {
        cfg.type = FLASH_TYPE_NOR;
    }

    s_capacity = opt->capacity;
    if (s_capacity == 0) {
        s_capacity = cfg.total_size >= 16384 ? 16384 : cfg.total_size;
    }
    s_capacity -= s_capacity % cfg.erase_size;
    if (s_capacity < cfg.erase_size * 2u) {
        s_capacity = cfg.erase_size * 2u;
    }
    if (s_capacity > cfg.total_size) {
        s_capacity = cfg.total_size - (cfg.total_size % cfg.erase_size);
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        return -1;
    }
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，仅重新扫描加载 */
    if (!app_env_u32("APP_REINIT", 0)) {
        flash_sim_erase(s_dev, 0, s_capacity);
    }

    if (fal_sim_port_init(s_dev, cfg.total_size, cfg.erase_size,
                          0, s_capacity, 0) != 0) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    if (fdb_kvdb_init(&s_kvdb, "app_kvdb", FAL_KVDB_PART_NAME, NULL, NULL)
        != FDB_NO_ERR) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    s_kvdb_inited = 1;
    return 0;
}

static void fdb_deinit_impl(void)
{
    if (s_kvdb_inited) {
        fdb_kvdb_deinit(&s_kvdb);
        s_kvdb_inited = 0;
    }
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *fdb_device_impl(void)
{
    return s_dev;
}

static int fdb_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!key || !val || len == 0) {
        return -1;
    }
    struct fdb_blob blob;
    return fdb_kv_set_blob(&s_kvdb, key,
                           fdb_blob_make(&blob, val, len)) == FDB_NO_ERR ? 0 : -1;
}

static int fdb_get_impl(const char *key, void *val, uint16_t *len)
{
    if (!key || !len) {
        return -1;
    }
    struct fdb_blob blob;
    size_t got = fdb_kv_get_blob(&s_kvdb, key,
                                 fdb_blob_make(&blob, val, *len));
    if (got == 0) {
        return -1;
    }
    *len = (uint16_t)blob.saved.len;
    return 0;
}

static int fdb_del_impl(const char *key)
{
    if (!key) {
        return -1;
    }
    return fdb_kv_del(&s_kvdb, key) == FDB_NO_ERR ? 0 : -1;
}

static const app_component_t s_flashdb_comp = {
    .id = "flashdb",
    .category = "kv",
    .name = "FlashDB (KVDB 开源组件)",
    .init = fdb_init_impl,
    .deinit = fdb_deinit_impl,
    .device = fdb_device_impl,
    .kv_set = fdb_set_impl,
    .kv_get = fdb_get_impl,
    .kv_del = fdb_del_impl,
};

static void __attribute__((constructor)) flashdb_ad_register(void)
{
    app_register(&s_flashdb_comp);
}
