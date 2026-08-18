/**
 * fatfs_ad.c - FatFs 开源文件系统适配器（适配层）
 *
 * 把 FatFs（ChaN/fatfs R0.16）封装为应用层统一接口 app_component_t。
 * 经移植层（fatfs_sim_port.c）对接模拟基座。组件源码零修改。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "fatfs_sim_port.h"

#include "app_register.h"
#include "app_util.h"
#include "flash_hal_adapter.h"

#define FATFS_AD_BIN "app_fatfs.bin"

static flash_dev_t *s_dev = NULL;
static flash_hal_t s_hal;
static FATFS s_fs;
static BYTE s_work[4096];
static int s_mounted = 0;

static int fatfs_init_impl(const app_option_t *opt)
{
    (void)opt;
    int reinit = app_env_u32("APP_REINIT", 0);
    if (!reinit) {
        remove(FATFS_AD_BIN);
    }
    /* 打开介质 -> 包装为统一 flash_hal_t -> 注册给移植层 */
    flash_config_t fc;
    memset(&fc, 0, sizeof(fc));
    fc.type = FLASH_TYPE_NOR;
    fc.total_size = 128 * 1024;
    fc.erase_size = 4096;
    fc.write_size = 1;
    fc.erase_cycles = 100000;
    fc.bin_path = FATFS_AD_BIN;
    s_dev = flash_sim_init(&fc);
    if (!s_dev) {
        return -1;
    }
    flash_hal_from_sim(s_dev, fc.total_size, fc.erase_size, fc.write_size, &s_hal);
    if (fatfs_port_init(&s_hal, 0) != 0) {
        return -1;
    }

    /* 掉电恢复：跳过格式化，直接挂载 */
    if (!reinit) {
        MKFS_PARM mk;
        memset(&mk, 0, sizeof(mk));
        mk.fmt = FM_FAT | FM_SFD;
        mk.n_fat = 1;
        FRESULT fr = f_mkfs("", &mk, s_work, sizeof(s_work));
        if (fr != FR_OK) {
            flash_sim_deinit(s_dev);
            s_dev = NULL;
            return -1;
        }
    }
    FRESULT fr = f_mount(&s_fs, "", 1);
    if (fr != FR_OK) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
        return -1;
    }
    s_mounted = 1;
    return 0;
}

static void fatfs_deinit_impl(void)
{
    if (s_mounted) {
        f_mount(NULL, "", 0);
        s_mounted = 0;
    }
    if (s_dev) {
        flash_sim_deinit(s_dev);
        s_dev = NULL;
    }
}

static flash_dev_t *fatfs_device_impl(void)
{
    return s_dev;
}

static int fatfs_write_impl(const char *name, const void *buf, uint32_t len)
{
    FIL f;
    FRESULT fr = f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        return -1;
    }
    UINT bw = 0;
    fr = f_write(&f, buf, (UINT)len, &bw);
    f_close(&f);
    return (fr == FR_OK && bw == (UINT)len) ? 0 : -1;
}

static int fatfs_read_impl(const char *name, void *buf, uint32_t *len)
{
    if (!len) {
        return -1;
    }
    FIL f;
    FRESULT fr = f_open(&f, name, FA_READ);
    if (fr != FR_OK) {
        return -1;
    }
    UINT rd = 0;
    fr = f_read(&f, buf, (UINT)*len, &rd);
    f_close(&f);
    if (fr != FR_OK) {
        return -1;
    }
    *len = (uint32_t)rd;
    return 0;
}

static int fatfs_append_impl(const char *name, const void *buf, uint32_t len)
{
    FIL f;
    FRESULT fr = f_open(&f, name, FA_OPEN_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        return -1;
    }
    fr = f_lseek(&f, f_size(&f));
    if (fr == FR_OK) {
        UINT bw = 0;
        fr = f_write(&f, buf, (UINT)len, &bw);
        if (fr != FR_OK || bw != (UINT)len) {
            fr = FR_INT_ERR;
        }
    }
    f_close(&f);
    return fr == FR_OK ? 0 : -1;
}

static int fatfs_delete_impl(const char *name)
{
    return f_unlink(name) == FR_OK ? 0 : -1;
}

static int fatfs_get_size_impl(const char *name, uint32_t *size)
{
    if (!size) {
        return -1;
    }
    FIL f;
    FRESULT fr = f_open(&f, name, FA_READ);
    if (fr != FR_OK) {
        return -1;
    }
    *size = (uint32_t)f_size(&f);
    f_close(&f);
    return 0;
}

static const app_component_t s_fatfs_comp = {
    .id = "fatfs",
    .category = "fs",
    .name = "FatFs (开源文件系统)",
    .init = fatfs_init_impl,
    .deinit = fatfs_deinit_impl,
    .device = fatfs_device_impl,
    .fs_write = fatfs_write_impl,
    .fs_read = fatfs_read_impl,
    .fs_append = fatfs_append_impl,
    .fs_delete = fatfs_delete_impl,
    .fs_get_size = fatfs_get_size_impl,
};

static void __attribute__((constructor)) fatfs_ad_register(void)
{
    app_register(&s_fatfs_comp);
}
