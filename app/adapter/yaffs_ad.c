/**
 * yaffs_ad.c - YAFFS 开源文件系统适配器（适配层）
 *
 * 把 YAFFS2 Direct（GPL v2）封装为应用层统一接口 app_component_t。
 * 经移植层（yaffs_sim_port.c）以 chunk+oob 布局对接模拟基座。
 * 组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yaffs_sim_port.h"
#include "yaffsfs.h"

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"

#define YAFFS_AD_BIN "app_yaffs.bin"
#define YAFFS_AD_ROOT "/flash"

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static int s_mounted = 0;

static int yaffs_init_impl(const app_option_t *opt)
{
    (void)opt;
    /* 掉电恢复（APP_REINIT=1）：保留介质内容，直接重新挂载 */
    if (!app_env_u32("APP_REINIT", 0)) {
        remove(YAFFS_AD_BIN);
    }
    /* 打开介质 -> 包装为统一 flash_hal_t -> 注册给移植层 */
    flash_config_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.type = FLASH_TYPE_NOR;
    fc.total_size = YAFFS_SIM_BLOCKS * YAFFS_SIM_CHUNKS_PER_BLOCK
                    * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES);
    fc.erase_size = YAFFS_SIM_CHUNKS_PER_BLOCK
                    * (YAFFS_SIM_CHUNK_BYTES + YAFFS_SIM_SPARE_BYTES);
    fc.write_size = 1;
    fc.erase_cycles = 100000;
    fc.bin_path = YAFFS_AD_BIN;
    s_dev = flash_sim_init(&fc);
    if (!s_dev) {
        return -1;
    }
    flash_hal_from_sim(s_dev, fc.total_size, fc.erase_size, fc.write_size, &s_hal);
    if (yaffs_port_init(&s_hal) != 0) {
        return -1;
    }
    if (yaffs_sim_start_up() != 0) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    if (yaffs_mount(YAFFS_AD_ROOT) < 0) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    s_mounted = 1;
    return 0;
}

static void yaffs_deinit_impl(void)
{
    if (s_mounted) {
        yaffs_unmount(YAFFS_AD_ROOT);
        s_mounted = 0;
    }
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *yaffs_device_impl(void)
{
    return s_dev;
}

static void yaffs_path(char *out, size_t sz, const char *name)
{
    snprintf(out, sz, YAFFS_AD_ROOT "/%s", name ? name : "");
}

static int yaffs_write_impl(const char *name, const void *buf, uint32_t len)
{
    char path[80];
    yaffs_path(path, sizeof(path), name);
    int fd = yaffs_open(path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        return -1;
    }
    int w = yaffs_write(fd, buf, len);
    yaffs_close(fd);
    return (w == (int)len) ? 0 : -1;
}

static int yaffs_read_impl(const char *name, void *buf, uint32_t *len)
{
    if (!len) {
        return -1;
    }
    char path[80];
    yaffs_path(path, sizeof(path), name);
    int fd = yaffs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    int rd = yaffs_read(fd, buf, *len);
    yaffs_close(fd);
    if (rd < 0) {
        return -1;
    }
    *len = (uint32_t)rd;
    return 0;
}

static int yaffs_append_impl(const char *name, const void *buf, uint32_t len)
{
    char path[80];
    yaffs_path(path, sizeof(path), name);
    int fd = yaffs_open(path, O_CREAT | O_RDWR | O_APPEND, 0666);
    if (fd < 0) {
        return -1;
    }
    int w = yaffs_write(fd, buf, len);
    yaffs_close(fd);
    return (w == (int)len) ? 0 : -1;
}

static int yaffs_delete_impl(const char *name)
{
    char path[80];
    yaffs_path(path, sizeof(path), name);
    return yaffs_unlink(path) >= 0 ? 0 : -1;
}

static int yaffs_get_size_impl(const char *name, uint32_t *size)
{
    if (!size) {
        return -1;
    }
    char path[80];
    yaffs_path(path, sizeof(path), name);
    int fd = yaffs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    struct yaffs_stat st;
    int rc = yaffs_fstat(fd, &st);
    yaffs_close(fd);
    if (rc < 0) {
        return -1;
    }
    *size = (uint32_t)st.st_size;
    return 0;
}

static const app_component_t s_yaffs_comp = {
    .id = "yaffs",
    .category = "fs",
    .name = "YAFFS (开源文件系统)",
    .init = yaffs_init_impl,
    .deinit = yaffs_deinit_impl,
    .device = yaffs_device_impl,
    .fs_write = yaffs_write_impl,
    .fs_read = yaffs_read_impl,
    .fs_append = yaffs_append_impl,
    .fs_delete = yaffs_delete_impl,
    .fs_get_size = yaffs_get_size_impl,
};

static void __attribute__((constructor)) yaffs_ad_register(void)
{
    app_register(&s_yaffs_comp);
}
