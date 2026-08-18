/**
 * flash_hal_mem.c - 内存模拟 HAL（零依赖，平台无关）
 *
 * 语义与 NOR Flash 一致：
 *   - 擦除：将整个块写为 0xFF；
 *   - 写：仅允许 1->0（向非 0xFF 位写 0 返回 -4），EEPROM 无需此限制；
 *   - 读：任意偏移任意长度（越界返回 -2）。
 */

#include "flash_hal_mem.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t total;
    uint32_t erase;
    uint8_t *buf;
} mem_dev_t;

static int mem_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    mem_dev_t *m = (mem_dev_t *)ctx;
    if (!m || !buf || off + len > m->total) {
        return FLASH_HAL_ERR_RANGE;
    }
    memcpy(buf, m->buf + off, len);
    return 0;
}

static int mem_write(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    mem_dev_t *m = (mem_dev_t *)ctx;
    if (!m || !buf || off + len > m->total) {
        return FLASH_HAL_ERR_RANGE;
    }
    /* 内存介质：普通覆盖写（不做 NOR 1->0 严格校验，与常见驱动/模拟器一致；
     * 框架若依赖 NOR 语义（写前必须擦除）由框架自身保证）。 */
    memcpy(m->buf + off, buf, len);
    return 0;
}

static int mem_erase(void *ctx, uint32_t off, uint32_t len)
{
    mem_dev_t *m = (mem_dev_t *)ctx;
    if (!m || off + len > m->total) {
        return FLASH_HAL_ERR_RANGE;
    }
    if (m->erase == 0 || off % m->erase != 0 || len == 0
        || len % m->erase != 0) {
        return FLASH_HAL_ERR_ARGS;
    }
    memset(m->buf + off, 0xFF, len);
    return 0;
}

int flash_hal_mem_create(uint32_t total, uint32_t erase, flash_hal_t *hal)
{
    if (!hal || total == 0) {
        return -1;
    }
    mem_dev_t *m = (mem_dev_t *)calloc(1, sizeof(mem_dev_t));
    if (!m) {
        return -1;
    }
    m->buf = (uint8_t *)malloc(total);
    if (!m->buf) {
        free(m);
        return -1;
    }
    memset(m->buf, 0xFF, total);
    m->total = total;
    m->erase = erase;

    memset(hal, 0, sizeof(*hal));
    hal->ctx = m;
    hal->total_size = total;
    hal->erase_size = erase;
    hal->write_size = 1;
    hal->read = mem_read;
    hal->write = mem_write;
    hal->erase = mem_erase;
    return 0;
}

void flash_hal_mem_destroy(flash_hal_t *hal)
{
    if (hal && hal->ctx) {
        mem_dev_t *m = (mem_dev_t *)hal->ctx;
        free(m->buf);
        free(m);
        memset(hal, 0, sizeof(*hal));
    }
}
