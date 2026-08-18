/**
 * ef_port.c - EasyFlash 移植层：对接本平台模拟基座 flash_sim
 *
 * 移植契约（EasyFlash 要求实现的 6 个接口）：
 *   ef_port_init      提供默认 ENV 集合
 *   ef_port_read      读取（EasyFlash 以 word 为单位描述，实际按字节访问）
 *   ef_port_write     写入（须先擦除；NOR 语义 1->0）
 *   ef_port_erase     擦除（地址须按 EF_ERASE_MIN_SIZE 对齐）
 *   ef_port_env_lock/unlock  ENV 缓存互斥（单线程仿真下为空实现）
 * 另需提供日志钩子 ef_log_debug / ef_log_info / ef_print。
 *
 * 地址映射：EasyFlash 内部使用的 addr 是"介质绝对偏移"（以 EF_START_ADDR
 * 为起点），与 flash_sim 的 offset 语义一致，故直接透传即可。
 *
 * 平台无关性：本文件是唯一与 flash_sim 耦合的适配层。移植到真实 MCU 时，
 * 仅需把 flash_sim_read/write/erase 替换为真实驱动调用，vendor/ 下的
 * EasyFlash 源码零修改。
 */

#include <easyflash.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "flash_hal.h"
#include "ef_port.h"

/* ---- 运行期配置（由 ef_port_setup 注入，供 ef_cfg.h 中的宏读取） ---- */
static const flash_hal_t *s_hal;      /* 统一 HAL 注册实例 */
static uint32_t     s_start_addr;     /* KV 区起始偏移 */
static uint32_t     s_area_size;      /* KV 区大小 */
static uint32_t     s_erase_size;     /* 擦除块大小 */
static uint32_t     s_verbose;        /* 是否输出 EasyFlash 内部日志 */

uint32_t ef_cfg_erase_min_size(void) { return s_erase_size; }
uint32_t ef_cfg_start_addr(void)     { return s_start_addr; }
uint32_t ef_cfg_env_area_size(void)  { return s_area_size; }

/* 默认 ENV 集合：本平台由测试程序动态写入，故留空 */
static const ef_env default_env_set[] = {
    {"ef_ver", "4.x", 0},
};

void ef_port_setup(const flash_hal_t *hal, uint32_t start_addr, uint32_t area_size,
                   uint32_t erase_size, int verbose)
{
    s_hal = hal;
    s_start_addr = start_addr;
    s_area_size = area_size;
    s_erase_size = erase_size;
    s_verbose = verbose ? 1u : 0u;
}

EfErrCode ef_port_init(ef_env const **default_env, size_t *default_env_size)
{
    *default_env = default_env_set;
    *default_env_size = sizeof(default_env_set) / sizeof(default_env_set[0]);
    return EF_NO_ERR;
}

EfErrCode ef_port_read(uint32_t addr, uint32_t *buf, size_t size)
{
    if (!s_hal || size == 0) { return EF_NO_ERR; }
    if (s_hal->read(s_hal->ctx, addr, buf, (uint32_t)size) != 0) {
        return EF_READ_ERR;
    }
    return EF_NO_ERR;
}

EfErrCode ef_port_erase(uint32_t addr, size_t size)
{
    if (!s_hal || size == 0) { return EF_NO_ERR; }

    /* 起始地址须按最小擦除单位对齐（EasyFlash 契约） */
    EF_ASSERT(addr % EF_ERASE_MIN_SIZE == 0);

    /*
     * EasyFlash 传入的 size 可能不是块大小整数倍（如只擦一个 ENV 区尾部），
     * 而模拟基座要求 len 按块对齐，故此处向上取整到整块。
     */
    uint32_t aligned = (uint32_t)size;
    uint32_t rem = aligned % s_erase_size;
    if (rem != 0) { aligned += s_erase_size - rem; }

    if (s_hal->erase(s_hal->ctx, addr, aligned) != 0) {
        return EF_ERASE_ERR;
    }
    return EF_NO_ERR;
}

EfErrCode ef_port_write(uint32_t addr, const uint32_t *buf, size_t size)
{
    if (!s_hal || size == 0) { return EF_NO_ERR; }
    if (s_hal->write(s_hal->ctx, addr, buf, (uint32_t)size) != 0) {
        return EF_WRITE_ERR;
    }
    return EF_NO_ERR;
}

/* 单线程仿真环境：无需真实互斥。移植到 RTOS 时替换为 mutex 获取/释放。 */
void ef_port_env_lock(void)   { }
void ef_port_env_unlock(void) { }

void ef_log_debug(const char *file, const long line, const char *format, ...)
{
    if (!s_verbose) { return; }
    va_list args;
    va_start(args, format);
    printf("    [EF-D] (%s:%ld) ", file, line);
    vprintf(format, args);
    va_end(args);
}

void ef_log_info(const char *format, ...)
{
    if (!s_verbose) { return; }
    va_list args;
    va_start(args, format);
    printf("    [EF-I] ");
    vprintf(format, args);
    va_end(args);
}

void ef_print(const char *format, ...)
{
    if (!s_verbose) { return; }
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}
