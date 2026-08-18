/**
 * tym_setting_ad.c - TYM Setting 组件适配器（适配层）
 *
 * 把 TYM Setting（ID 索引静态表 + RAM 全镜像 + 延时批量整页回写）
 * 封装为应用层统一接口 app_component_t（KV 语义）。
 *
 * TYM Setting 的 key 是编译期固定 eSettingId 枚举，应用层是字符串 key，
 * 适配层把 key hash 映射到 ID 空间。每次 kv_set 后立即触发
 * SettingSrv_BookkeepingEx()（整页擦除 + 全表回写）保证落盘。
 *
 * 注意：TYM 写放大极大（改 1 项 = 擦整区 + 写全表），压测轮数宜小。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_setting_idle_activity.h"
#include "SettingSrv_priv.h"

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"
#include "tym_setting_sim_port.h"

#define TYM_AD_BIN "app_tym_setting.bin"

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static int s_inited = 0;

/*
 * key -> eSettingId 映射。
 * TYM Setting 各槽位大小固定，通用 KV 压测 vlen 统一，若散落到不同大小
 * 槽位会导致 set/get 长度不匹配；且仅 NVM 槽位会落 Flash（掉电恢复）。
 * 因此适配器只使用 4 个 4 字节 NVM 槽位（masterVol + audioGain1~3），
 * 按 key 序号循环分配，避免碰撞、长度错配与掉电丢失。
 */
static eSettingId tym_ad_key2id(const char *key)
{
    static const eSettingId s_nvm_ids[4] = {
        SETID_MASTER_VOL, SETID_AUDIO_GAIN_1,
        SETID_AUDIO_GAIN_2, SETID_AUDIO_GAIN_3,
    };
    uint32_t n = 0;
    const char *p = key;

    while (*p >= '0' && *p <= '9') {
        n = n * 10u + (uint32_t)(*p - '0');
        p++;
    }
    /* 兼容 "k0".."k9" 形式：跳过非数字前缀 */
    if (*key && p == key) {
        const char *q = key;
        while (*q && (*q < '0' || *q > '9')) { q++; }
        n = (uint32_t)strtoul(q, NULL, 10);
    }
    return s_nvm_ids[n % 4u];
}

static int tym_ad_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, TYM_AD_BIN, FLASH_TYPE_NOR);
    if (cfg.type == FLASH_TYPE_EEPROM) {
        cfg.type = FLASH_TYPE_NOR;
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

    flash_hal_from_sim(s_dev, cfg.total_size, cfg.erase_size, cfg.write_size, &s_hal);
    tym_setting_sim_setup(&s_hal, 0, capacity, cfg.erase_size);
    SettingSrv_Init();
    s_inited = 1;
    return 0;
}

static void tym_ad_deinit_impl(void)
{
    s_inited = 0;
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *tym_ad_device_impl(void)
{
    return s_dev;
}

static int tym_ad_set_impl(const char *key, const void *val, uint16_t len)
{
    eSettingId id;
    uint32_t item_size;

    if (!key || !val || len == 0 || !s_inited) {
        return -1;
    }
    id = tym_ad_key2id(key);
    item_size = Setting_GetSize(id);
    if (item_size == 0) {
        return -1;
    }
    if (len > item_size) {
        len = (uint16_t)item_size;
    }
    Setting_Set(id, val);
    SettingSrv_BookkeepingEx();   /* 立即落盘（整页回写） */
    return 0;
}

static int tym_ad_get_impl(const char *key, void *val, uint16_t *len)
{
    eSettingId id;
    const void *p;
    uint32_t item_size;

    if (!key || !len || !s_inited) {
        return -1;
    }
    id = tym_ad_key2id(key);
    item_size = Setting_GetSize(id);
    if (item_size == 0) {
        return -1;
    }
    p = Setting_Get(id);
    if (p == NULL) {
        return -1;
    }
    if (item_size > *len) {
        return -1;
    }
    memcpy(val, p, item_size);
    *len = (uint16_t)item_size;
    return 0;
}

static int tym_ad_del_impl(const char *key)
{
    eSettingId id;

    if (!key || !s_inited) {
        return -1;
    }
    id = tym_ad_key2id(key);
    if (Setting_GetSize(id) == 0) {
        return -1;
    }
    Setting_Reset(id);
    SettingSrv_BookkeepingEx();
    return 0;
}

static const app_component_t s_tym_setting_comp = {
    .id = "tym_setting",
    .category = "kv",
    .name = "TYM Setting (ID静态表/RAM镜像)",
    .init = tym_ad_init_impl,
    .deinit = tym_ad_deinit_impl,
    .device = tym_ad_device_impl,
    .kv_set = tym_ad_set_impl,
    .kv_get = tym_ad_get_impl,
    .kv_del = tym_ad_del_impl,
};

static void __attribute__((constructor)) tym_setting_ad_register(void)
{
    app_register(&s_tym_setting_comp);
}
