/**
 * fastflash_ad.c - fast_flashdb_table 组件适配器（适配层）
 *
 * 把轻量表存储组件 fast_flashdb_table（表名 + 索引语义）适配为应用层
 * 统一接口 app_component_t。
 *
 * 适配策略（单表多索引，降低组件固有写放大）：
 *   - 所有 KV 记录存进同一张表（"kv"），index = key_id % MAX_STRUCTS；
 *   - set 首次按序追加（write_table_data），已存在则按索引覆盖
 *     （write_table_data_by_index，整表迁移一次）；
 *   - get 按索引读取；del 将目标记录置全 0xFF（组件无按索引删除 API）。
 *   - 写入前空间不足先触发 GC 回收孤儿空间。
 *
 * 组件限制：MAX_TABLES_ALL_SECTOR=24 张表、FLASH_SECTOR_SIZE=4KB，
 * 因此单表 + struct_size 固定（应用层统一 vlen）即可规避表数上限。
 * 组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fast_flash_core.h"
#include "fast_flash_types.h"
#include "fastflash_sim_port.h"

#include "app_register.h"
#include "app_util.h"

#define FLT_AD_BIN "app_fastflash.bin"
#define FLT_TABLE "kv"
#define FLT_MAX_STRUCTS 64u

static uint32_t s_total = 0;
static uint16_t s_struct_size = 0;

static void flt_index(const char *key, uint32_t *idx)
{
    const char *dig = key;
    while (dig && *dig && !(*dig >= '0' && *dig <= '9')) {
        dig++;
    }
    long id = (dig && *dig) ? strtol(dig, NULL, 10) : 0;
    if (id < 0) {
        id = 0;
    }
    *idx = (uint32_t)id % FLT_MAX_STRUCTS;
}

/* 表内第 index 条是否已写（struct_nums > index） */
static int flt_index_written(uint32_t index)
{
    flash_table_t info;
    if (fast_flash_get_table_info(FLT_TABLE, &info) != 0) {
        return 0;
    }
    /* 通过读表头获取 struct_nums */
    flash_dev_t *dev = fast_flash_sim_device();
    if (!dev) {
        return 0;
    }
    uint8_t hdr[sizeof(table_header_t)];
    if (flash_sim_read(dev, info.addr, hdr, sizeof(hdr)) != FLASH_OK) {
        return 0;
    }
    table_header_t header;
    memcpy(&header, hdr, sizeof(header));
    return index < header.struct_nums;
}

static int flt_init_impl(const app_option_t *opt)
{
    s_struct_size = (uint16_t)(opt->vlen ? opt->vlen : 32u);
    /* fast_flashdb_table 为日志式设计：每次覆盖写迁移整表并重写 manager
     * 表，写放大显著。默认给足 2MB 容量避免 durability/update 任务空间
     * 耗尽（用户可通过 SIM_TOTAL 显式调整）。 */
    if (!getenv("SIM_TOTAL")) {
        setenv("SIM_TOTAL", "2097152", 1);
    }
    s_total = app_env_u32("SIM_TOTAL", 2 * 1024 * 1024);
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，直接重新初始化 */
    if (!app_env_u32("APP_REINIT", 0)) {
        remove(FLT_AD_BIN);
    }
    if (fast_flash_sim_init_device(FLT_AD_BIN) != 0) {
        return -1;
    }
    if (fast_flash_init(&sim_flash_ops, s_total, true) != 0) {
        fast_flash_sim_deinit_device();
        return -1;
    }
    return 0;
}

static void flt_deinit_impl(void)
{
    fast_flash_sim_deinit_device();
}

static flash_dev_t *flt_device_impl(void)
{
    return fast_flash_sim_device();
}

static int flt_set_impl(const char *key, const void *val, uint16_t len)
{
    if (!val || len == 0) {
        return -1;
    }
    if (len != s_struct_size) {
        return -1; /* 单表要求 struct_size 固定（应用层统一 vlen） */
    }
    uint32_t index;
    flt_index(key, &index);

    /* 表不存在则创建 */
    if (!fast_flash_table_exists(FLT_TABLE)) {
        if (fast_flash_create_table(FLT_TABLE, len, FLT_MAX_STRUCTS) != 0) {
            return -1;
        }
    }
    /* index 未写则按序追加（建表后需顺序填充） */
    if (!flt_index_written(index)) {
        return fast_flash_write_table_data(FLT_TABLE, val, len) == 0 ? 0 : -1;
    }
    /* 已写则按索引覆盖（整表迁移），空间不足先 GC */
    if (fast_flash_write_table_data_by_index(FLT_TABLE, index, val, len) == 0) {
        return 0;
    }
    fast_flash_gc();
    return fast_flash_write_table_data_by_index(FLT_TABLE, index, val, len)
           == 0 ? 0 : -1;
}

static int flt_get_impl(const char *key, void *val, uint16_t *len)
{
    if (!val || !len) {
        return -1;
    }
    if (*len != s_struct_size) {
        return -1;
    }
    uint32_t index;
    flt_index(key, &index);
    if (!fast_flash_table_exists(FLT_TABLE) || !flt_index_written(index)) {
        return -1;
    }
    return fast_flash_read_table_data(FLT_TABLE, index, val, *len) == 0 ? 0 : -1;
}

static int flt_del_impl(const char *key)
{
    /* 组件无按索引删除 API：将目标记录覆盖为全 0xFF 表示已删 */
    uint32_t index;
    flt_index(key, &index);
    if (!fast_flash_table_exists(FLT_TABLE) || !flt_index_written(index)) {
        return 0; /* 不存在视为删除成功 */
    }
    uint8_t *buf = (uint8_t *)calloc(1, s_struct_size);
    if (!buf) {
        return -1;
    }
    memset(buf, 0xFF, s_struct_size);
    int rc = fast_flash_write_table_data_by_index(FLT_TABLE, index, buf,
                                                  s_struct_size) == 0 ? 0 : -1;
    free(buf);
    return rc;
}

static const app_component_t s_fastflash_comp = {
    .id = "fastflash",
    .category = "kv",
    .name = "fast_flashdb_table 组件",
    .init = flt_init_impl,
    .deinit = flt_deinit_impl,
    .device = flt_device_impl,
    .kv_set = flt_set_impl,
    .kv_get = flt_get_impl,
    .kv_del = flt_del_impl,
};

static void __attribute__((constructor)) fastflash_ad_register(void)
{
    app_register(&s_fastflash_comp);
}
