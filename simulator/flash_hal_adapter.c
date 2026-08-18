/**
 * flash_hal_adapter.c - 模拟基座 -> 统一 flash_hal_t 适配器
 *
 * 把 flash_sim_read/write/erase 包装成 flash_hal_t 的三个回调，
 * 使平台内测试程序可以用"注册 HAL"的方式驱动任意框架，与真实
 * 平台上的注册方式完全一致（真实平台只需换一套 read/write/erase 实现）。
 */

#include "flash_hal_adapter.h"

#include <string.h>

static int sim_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    flash_dev_t *dev = (flash_dev_t *)ctx;
    return (int)flash_sim_read(dev, off, buf, len);
}

static int sim_write(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    flash_dev_t *dev = (flash_dev_t *)ctx;
    return (int)flash_sim_write(dev, off, buf, len);
}

static int sim_erase(void *ctx, uint32_t off, uint32_t len)
{
    flash_dev_t *dev = (flash_dev_t *)ctx;
    return (int)flash_sim_erase(dev, off, len);
}

void flash_hal_from_sim(flash_dev_t *dev, uint32_t total, uint32_t erase,
                        uint32_t write, flash_hal_t *hal)
{
    if (!hal) { return; }
    memset(hal, 0, sizeof(*hal));
    hal->ctx = dev;
    hal->total_size = total;
    hal->erase_size = erase;
    hal->write_size = write;
    hal->read = sim_read;
    hal->write = sim_write;
    hal->erase = sim_erase;
}
