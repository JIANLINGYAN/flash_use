/**
 * baremetal_ad.c - 裸机结构体配置框架适配器（适配层）
 *
 * 把 frameworks/baremetal（A/B 双备份 + CRC + 单调序号）封装为应用层
 * 统一接口 app_component_t。bm_save/bm_load 映射到 bm_config_save/load。
 * 组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"
#include "bm_config.h"

#define BM_AD_BIN "app_baremetal.bin"
#define BM_AD_PAYLOAD_MAX 256u

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static bm_config_t s_ctx;
static uint32_t s_payload_len = 0;

static int bm_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, BM_AD_BIN, FLASH_TYPE_NOR);

    s_payload_len = opt->vlen ? opt->vlen : 64;
    if (s_payload_len > BM_AD_PAYLOAD_MAX) {
        s_payload_len = BM_AD_PAYLOAD_MAX;
    }

    uint32_t erase = cfg.erase_size ? cfg.erase_size : 4096u;
    uint32_t part = erase * 2u;               /* 单分区两块，保证容纳头部+payload */
    if (part < sizeof(bm_header_t) + s_payload_len) {
        part = ((sizeof(bm_header_t) + s_payload_len + erase - 1u) / erase) * erase;
    }
    if (part * 2u > cfg.total_size) {
        return -1;
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        return -1;
    }
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，仅重新扫描加载 */
    if (!app_env_u32("APP_REINIT", 0)) {
        flash_sim_erase(s_dev, 0, cfg.total_size);
    }
    flash_hal_from_sim(s_dev, cfg.total_size, cfg.erase_size, cfg.write_size, &s_hal);
    return bm_config_init(&s_ctx, &s_hal, 0, part, part, s_payload_len)
           == 0 ? 0 : -1;
}

static void bm_deinit_impl(void)
{
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *bm_device_impl(void)
{
    return s_dev;
}

static int bm_save_impl(const void *data, uint32_t len)
{
    if (!s_dev || !data || len != s_payload_len) {
        return -1;
    }
    return bm_config_save(&s_ctx, data) == 0 ? 0 : -1;
}

static int bm_load_impl(void *data, uint32_t len)
{
    if (!s_dev || !data || len != s_payload_len) {
        return -1;
    }
    return bm_config_load(&s_ctx, data) == 0 ? 0 : -1;
}

static const app_component_t s_baremetal_comp = {
    .id = "baremetal",
    .category = "baremetal",
    .name = "裸机结构体配置 (A/B+CRC)",
    .init = bm_init_impl,
    .deinit = bm_deinit_impl,
    .device = bm_device_impl,
    .bm_save = bm_save_impl,
    .bm_load = bm_load_impl,
};

static void __attribute__((constructor)) baremetal_ad_register(void)
{
    app_register(&s_baremetal_comp);
}
