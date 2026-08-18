/**
 * littlefs_ad.c - LittleFS 开源文件系统适配器（适配层）
 *
 * 把 littlefs（littlefs-project/littlefs v2.x）封装为应用层统一接口
 * app_component_t。经移植层（littlefs_sim_port.c）对接模拟基座。
 * 组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lfs.h"
#include "littlefs_sim_port.h"

#include "app_register.h"
#include "app_util.h"

#define LFS_AD_BIN "app_littlefs.bin"

static flash_dev_t *s_dev = NULL;
static lfs_t s_lfs;
static struct lfs_config s_cfg;
static int s_mounted = 0;

static int lfs_init_impl(const app_option_t *opt)
{
    (void)opt;
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，直接重新挂载 */
    if (!app_env_u32("APP_REINIT", 0)) {
        remove(LFS_AD_BIN);
    }
    if (littlefs_sim_init_device(LFS_AD_BIN, &s_cfg) != 0) {
        return -1;
    }
    s_dev = littlefs_sim_device();

    int rc = lfs_mount(&s_lfs, &s_cfg);
    if (rc != LFS_ERR_OK) {
        /* 掉电恢复时禁止格式化（介质损坏应报错而非重建） */
        if (app_env_u32("APP_REINIT", 0)) {
            littlefs_sim_deinit_device();
            s_dev = NULL;
            return -1;
        }
        rc = lfs_format(&s_lfs, &s_cfg);
        if (rc != LFS_ERR_OK) {
            littlefs_sim_deinit_device();
            s_dev = NULL;
            return -1;
        }
        rc = lfs_mount(&s_lfs, &s_cfg);
        if (rc != LFS_ERR_OK) {
            littlefs_sim_deinit_device();
            s_dev = NULL;
            return -1;
        }
    }
    s_mounted = 1;
    return 0;
}

static void lfs_deinit_impl(void)
{
    if (s_mounted) {
        lfs_unmount(&s_lfs);
        s_mounted = 0;
    }
    if (s_dev) {
        littlefs_sim_deinit_device();
        s_dev = NULL;
    }
}

static flash_dev_t *lfs_device_impl(void)
{
    return s_dev;
}

static int lfs_write_impl(const char *name, const void *buf, uint32_t len)
{
    lfs_file_t f;
    int rc = lfs_file_open(&s_lfs, &f, name,
                           LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
    if (rc != LFS_ERR_OK) {
        return -1;
    }
    lfs_ssize_t w = lfs_file_write(&s_lfs, &f, buf, len);
    lfs_file_close(&s_lfs, &f);
    return (w == (lfs_ssize_t)len) ? 0 : -1;
}

static int lfs_read_impl(const char *name, void *buf, uint32_t *len)
{
    if (!len) {
        return -1;
    }
    lfs_file_t f;
    int rc = lfs_file_open(&s_lfs, &f, name, LFS_O_RDONLY);
    if (rc != LFS_ERR_OK) {
        return -1;
    }
    lfs_soff_t fsz = lfs_file_size(&s_lfs, &f);
    if (fsz < 0) {
        lfs_file_close(&s_lfs, &f);
        return -1;
    }
    uint32_t want = (*len < (uint32_t)fsz) ? *len : (uint32_t)fsz;
    lfs_ssize_t rd = lfs_file_read(&s_lfs, &f, buf, want);
    lfs_file_close(&s_lfs, &f);
    if (rd < 0) {
        return -1;
    }
    *len = (uint32_t)rd;
    return 0;
}

static int lfs_append_impl(const char *name, const void *buf, uint32_t len)
{
    lfs_file_t f;
    int rc = lfs_file_open(&s_lfs, &f, name,
                           LFS_O_RDWR | LFS_O_CREAT | LFS_O_APPEND);
    if (rc != LFS_ERR_OK) {
        return -1;
    }
    lfs_ssize_t w = lfs_file_write(&s_lfs, &f, buf, len);
    lfs_file_close(&s_lfs, &f);
    return (w == (lfs_ssize_t)len) ? 0 : -1;
}

static int lfs_delete_impl(const char *name)
{
    return lfs_remove(&s_lfs, name) == LFS_ERR_OK ? 0 : -1;
}

static int lfs_get_size_impl(const char *name, uint32_t *size)
{
    if (!size) {
        return -1;
    }
    struct lfs_info info;
    int rc = lfs_stat(&s_lfs, name, &info);
    if (rc != LFS_ERR_OK) {
        return -1;
    }
    *size = (uint32_t)info.size;
    return 0;
}

static const app_component_t s_littlefs_comp = {
    .id = "littlefs",
    .category = "fs",
    .name = "LittleFS (开源文件系统)",
    .init = lfs_init_impl,
    .deinit = lfs_deinit_impl,
    .device = lfs_device_impl,
    .fs_write = lfs_write_impl,
    .fs_read = lfs_read_impl,
    .fs_append = lfs_append_impl,
    .fs_delete = lfs_delete_impl,
    .fs_get_size = lfs_get_size_impl,
};

static void __attribute__((constructor)) littlefs_ad_register(void)
{
    app_register(&s_littlefs_comp);
}
