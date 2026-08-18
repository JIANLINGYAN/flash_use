/**
 * easyflash_ad.c - EasyFlash 组件适配器（适配层）
 *
 * 把开源 KV 组件 EasyFlash（armink/EasyFlash V4.x NG 模式）封装为
 * 应用层统一接口 app_component_t，组件源码零修改，仅经移植层
 * （ef_port.c）对接模拟基座。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <easyflash.h>

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"
#include "ef_port.h"

#define EF_AD_BIN "app_easyflash.bin"

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static uint32_t s_capacity = 0;

static int ef_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, EF_AD_BIN, FLASH_TYPE_NOR);
    if (cfg.type == FLASH_TYPE_EEPROM) {
        /* EasyFlash 依赖块擦除语义，EEPROM 自动切换 NOR */
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
    flash_hal_from_sim(s_dev, cfg.total_size, cfg.erase_size, cfg.write_size, &s_hal);

    ef_port_setup(&s_hal, 0, s_capacity, cfg.erase_size, 0);
    if (easyflash_init() != EF_NO_ERR) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    return 0;
}

static void ef_deinit_impl(void)
{
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *ef_device_impl(void)
{
    return s_dev;
}

static int ef_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!key || !val || len == 0) {
        return -1;
    }
    return ef_set_env_blob(key, val, len) == EF_NO_ERR ? 0 : -1;
}

static int ef_get_impl(const char *key, void *val, uint16_t *len)
{
    if (!key || !len) {
        return -1;
    }
    size_t saved = 0;
    size_t got = ef_get_env_blob(key, val, *len, &saved);
    if (got == 0) {
        return -1; /* key 不存在 */
    }
    *len = (uint16_t)saved;
    return 0;
}

static int ef_del_impl(const char *key)
{
    if (!key) {
        return -1;
    }
    return ef_del_env(key) == EF_NO_ERR ? 0 : -1;
}

static const app_component_t s_easyflash_comp = {
    .id = "easyflash",
    .category = "kv",
    .name = "EasyFlash (ENV/KV 开源组件)",
    .init = ef_init_impl,
    .deinit = ef_deinit_impl,
    .device = ef_device_impl,
    .kv_set = ef_set_impl,
    .kv_get = ef_get_impl,
    .kv_del = ef_del_impl,
};

static void __attribute__((constructor)) easyflash_ad_register(void)
{
    app_register(&s_easyflash_comp);
}
