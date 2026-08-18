/**
 * nvdm_ad.c - Airoha NVDM 组件适配器（适配层）
 *
 * 把 Airoha NVDM（KV/裸机持久化框架）封装为应用层统一接口
 * app_component_t，组件源码零修改，仅经移植层（nvdm_sim_port.c）
 * 对接模拟基座。
 *
 * NVDM 的 KV 语义是"组(group)+项(item)"两级命名，应用层统一接口是
 * 单 key，因此适配层把 key 直接作为 item 名，group 统一为 "app"。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvdm.h"

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"
#include "nvdm_sim_port.h"

#define NVDM_AD_BIN "app_nvdm.bin"
#define NVDM_AD_GROUP "app"
#define NVDM_AD_ITEM_COUNT 256

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static uint32_t s_capacity = 0;
static int s_inited = 0;

static int nvdm_ad_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, NVDM_AD_BIN, FLASH_TYPE_NOR);
    if (cfg.type == FLASH_TYPE_EEPROM) {
        /* NVDM 依赖块擦除语义，EEPROM 自动切换 NOR */
        cfg.type = FLASH_TYPE_NOR;
    }
    /* NVDM 按字节写（写入仅允许 1->0），强制最小写入单位为 1 */
    if (cfg.write_size != 1) {
        cfg.write_size = 1;
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
    nvdm_sim_setup(&s_hal, 0, s_capacity, cfg.erase_size, NVDM_AD_ITEM_COUNT);
    if (nvdm_init() != NVDM_STATUS_OK) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    s_inited = 1;
    return 0;
}

static void nvdm_ad_deinit_impl(void)
{
    s_inited = 0;
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *nvdm_ad_device_impl(void)
{
    return s_dev;
}

static int nvdm_ad_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!key || !val || len == 0 || !s_inited) {
        return -1;
    }
    return nvdm_write_data_item(NVDM_AD_GROUP, key,
                                NVDM_DATA_ITEM_TYPE_RAW_DATA,
                                (const uint8_t *)val, len) == NVDM_STATUS_OK ? 0 : -1;
}

static int nvdm_ad_get_impl(const char *key, void *val, uint16_t *len)
{
    if (!key || !len || !s_inited) {
        return -1;
    }
    uint32_t rl = *len;
    if (nvdm_read_data_item(NVDM_AD_GROUP, key,
                            (uint8_t *)val, &rl) != NVDM_STATUS_OK) {
        return -1;
    }
    *len = (uint16_t)rl;
    return 0;
}

static int nvdm_ad_del_impl(const char *key)
{
    if (!key || !s_inited) {
        return -1;
    }
    return nvdm_delete_data_item(NVDM_AD_GROUP, key) == NVDM_STATUS_OK ? 0 : -1;
}

static const app_component_t s_nvdm_comp = {
    .id = "nvdm",
    .category = "kv",
    .name = "Airoha NVDM (KV/裸机持久化)",
    .init = nvdm_ad_init_impl,
    .deinit = nvdm_ad_deinit_impl,
    .device = nvdm_ad_device_impl,
    .kv_set = nvdm_ad_set_impl,
    .kv_get = nvdm_ad_get_impl,
    .kv_del = nvdm_ad_del_impl,
};

static void __attribute__((constructor)) nvdm_ad_register(void)
{
    app_register(&s_nvdm_comp);
}
