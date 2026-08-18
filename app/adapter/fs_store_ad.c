/**
 * fs_store_ad.c - 自研文件系统框架适配器（适配层）
 *
 * 把 frameworks/fs/fs_store 封装为应用层统一接口 app_component_t。
 * fs_write/fs_read/fs_append/fs_delete 映射到 fs_store 的对应 API。
 * 组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"
#include "fs_store.h"

#define FS_AD_BIN "app_fs_store.bin"

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static uint32_t s_block_size = 0;

static int fs_init_impl(const app_option_t *opt)
{
    flash_config_t cfg;
    app_sim_make_config(&cfg, FS_AD_BIN, FLASH_TYPE_NOR);

    s_block_size = cfg.erase_size ? cfg.erase_size : 4096u;
    /* FS 为块式文件系统：每文件至少 1 个数据块 + 1 个 FAT 块。
     * 未显式指定容量时用整片介质；显式指定时按块对齐，并保证能
     * 容纳 APP_ITEMS 个文件，否则回退整片容量。 */
    uint32_t capacity = opt->capacity;
    if (capacity == 0) {
        capacity = cfg.total_size;
    }
    capacity -= capacity % s_block_size;
    uint32_t need = s_block_size * (opt->items + 1u);
    if (capacity < need) {
        capacity = cfg.total_size - (cfg.total_size % s_block_size);
    }
    if (capacity < s_block_size * 2u) {
        capacity = s_block_size * 2u;
    }
    if (capacity > cfg.total_size) {
        capacity = cfg.total_size - (cfg.total_size % s_block_size);
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        return -1;
    }
    flash_hal_from_sim(s_dev, cfg.total_size, cfg.erase_size, cfg.write_size, &s_hal);
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，仅重新挂载加载 */
    if (!app_env_u32("APP_REINIT", 0)) {
        if (fs_format(&s_hal, 0, capacity, s_block_size) != FS_OK) {
            flash_sim_deinit(s_dev);
            s_dev = NULL;
            return -1;
        }
    }
    return fs_init(&s_hal, 0, capacity, s_block_size) == FS_OK ? 0 : -1;
}

static void fs_deinit_impl(void)
{
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *fs_device_impl(void)
{
    return s_dev;
}

static int fs_write_impl(const char *name, const void *buf, uint32_t len)
{
    if (!s_dev || !name || !buf) {
        return -1;
    }
    return fs_write(&s_hal, name, buf, len) == FS_OK ? 0 : -1;
}

static int fs_read_impl(const char *name, void *buf, uint32_t *len)
{
    if (!s_dev || !name || !buf || !len) {
        return -1;
    }
    return fs_read(&s_hal, name, buf, 0, len) == FS_OK ? 0 : -1;
}

static int fs_append_impl(const char *name, const void *buf, uint32_t len)
{
    if (!s_dev || !name || !buf) {
        return -1;
    }
    return fs_append(&s_hal, name, buf, len) == FS_OK ? 0 : -1;
}

static int fs_delete_impl(const char *name)
{
    if (!s_dev || !name) {
        return -1;
    }
    return fs_delete(&s_hal, name) == FS_OK ? 0 : -1;
}

static int fs_get_size_impl(const char *name, uint32_t *size)
{
    if (!s_dev || !name || !size) {
        return -1;
    }
    return fs_get_size(&s_hal, name, size) == FS_OK ? 0 : -1;
}

static const app_component_t s_fs_store_comp = {
    .id = "fs",
    .category = "fs",
    .name = "自研文件系统 (fs_store)",
    .init = fs_init_impl,
    .deinit = fs_deinit_impl,
    .device = fs_device_impl,
    .fs_write = fs_write_impl,
    .fs_read = fs_read_impl,
    .fs_append = fs_append_impl,
    .fs_delete = fs_delete_impl,
    .fs_get_size = fs_get_size_impl,
};

static void __attribute__((constructor)) fs_store_ad_register(void)
{
    app_register(&s_fs_store_comp);
}
