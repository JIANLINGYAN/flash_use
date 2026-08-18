/**
 * spiffs_ad.c - SPIFFS 开源文件系统适配器（适配层）
 *
 * 把 SPIFFS（pellepl/spiffs）封装为应用层统一接口 app_component_t。
 * 经移植层（spiffs_sim_port.c）对接模拟基座。组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spiffs.h"
#include "spiffs_sim_port.h"

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"

#define SPIFFS_AD_BIN "app_spiffs.bin"

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static int s_mounted = 0;

static int spiffs_init_impl(const app_option_t *opt)
{
    (void)opt;
    int reinit = app_env_u32("APP_REINIT", 0);
    if (!reinit) {
        remove(SPIFFS_AD_BIN);
    }
    /* 打开介质 -> 包装为统一 flash_hal_t -> 注册给移植层 */
    flash_config_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.type = FLASH_TYPE_NOR;
    fc.total_size = 128 * 1024;
    fc.erase_size = 4096;
    fc.write_size = 1;
    fc.erase_cycles = 100000;
    fc.bin_path = SPIFFS_AD_BIN;
    s_dev = flash_sim_init(&fc);
    if (!s_dev) {
        return -1;
    }
    flash_hal_from_sim(s_dev, fc.total_size, fc.erase_size, fc.write_size, &s_hal);
    if (spiffs_port_init(&s_hal) != 0) {
        return -1;
    }

    s32_t rc = spiffs_sim_mount();
    if (rc != SPIFFS_OK) {
        /* 掉电恢复时禁止格式化（介质损坏应报错而非重建） */
        if (reinit) {
            flash_sim_deinit(s_dev);
            s_dev = NULL;
            return -1;
        }
        rc = spiffs_sim_format();
        if (rc != SPIFFS_OK) {
            flash_sim_deinit(s_dev);
            s_dev = NULL;
            return -1;
        }
        rc = spiffs_sim_mount();
        if (rc != SPIFFS_OK) {
            flash_sim_deinit(s_dev);
            s_dev = NULL;
            return -1;
        }
    }
    s_mounted = 1;
    return 0;
}

static void spiffs_deinit_impl(void)
{
    if (s_mounted) {
        spiffs_sim_unmount();
        s_mounted = 0;
    }
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *spiffs_device_impl(void)
{
    return s_dev;
}

static spiffs *spiffs_fs(void)
{
    return (spiffs *)spiffs_sim_fs();
}

static int spiffs_write_impl(const char *name, const void *buf, uint32_t len)
{
    spiffs *fs = spiffs_fs();
    spiffs_file f = SPIFFS_open(fs, name, SPIFFS_O_CREAT | SPIFFS_O_RDWR
                                | SPIFFS_O_TRUNC, 0);
    if (f < 0) {
        return -1;
    }
    s32_t w = SPIFFS_write(fs, f, (void *)buf, (s32_t)len);
    SPIFFS_close(fs, f);
    return (w == (s32_t)len) ? 0 : -1;
}

static int spiffs_read_impl(const char *name, void *buf, uint32_t *len)
{
    if (!len) {
        return -1;
    }
    spiffs *fs = spiffs_fs();
    spiffs_file f = SPIFFS_open(fs, name, SPIFFS_O_RDONLY, 0);
    if (f < 0) {
        return -1;
    }
    s32_t rd = SPIFFS_read(fs, f, buf, (s32_t)*len);
    SPIFFS_close(fs, f);
    if (rd < 0) {
        return -1;
    }
    *len = (uint32_t)rd;
    return 0;
}

static int spiffs_append_impl(const char *name, const void *buf, uint32_t len)
{
    spiffs *fs = spiffs_fs();
    spiffs_file f = SPIFFS_open(fs, name, SPIFFS_O_CREAT | SPIFFS_O_RDWR
                                | SPIFFS_O_APPEND, 0);
    if (f < 0) {
        return -1;
    }
    s32_t w = SPIFFS_write(fs, f, (void *)buf, (s32_t)len);
    SPIFFS_close(fs, f);
    return (w == (s32_t)len) ? 0 : -1;
}

static int spiffs_delete_impl(const char *name)
{
    return SPIFFS_remove(spiffs_fs(), name) == SPIFFS_OK ? 0 : -1;
}

static int spiffs_get_size_impl(const char *name, uint32_t *size)
{
    if (!size) {
        return -1;
    }
    spiffs *fs = spiffs_fs();
    spiffs_file f = SPIFFS_open(fs, name, SPIFFS_O_RDONLY, 0);
    if (f < 0) {
        return -1;
    }
    spiffs_stat st;
    s32_t rc = SPIFFS_fstat(fs, f, &st);
    SPIFFS_close(fs, f);
    if (rc != SPIFFS_OK) {
        return -1;
    }
    *size = (uint32_t)st.size;
    return 0;
}

static const app_component_t s_spiffs_comp = {
    .id = "spiffs",
    .category = "fs",
    .name = "SPIFFS (开源文件系统)",
    .init = spiffs_init_impl,
    .deinit = spiffs_deinit_impl,
    .device = spiffs_device_impl,
    .fs_write = spiffs_write_impl,
    .fs_read = spiffs_read_impl,
    .fs_append = spiffs_append_impl,
    .fs_delete = spiffs_delete_impl,
    .fs_get_size = spiffs_get_size_impl,
};

static void __attribute__((constructor)) spiffs_ad_register(void)
{
    app_register(&s_spiffs_comp);
}
