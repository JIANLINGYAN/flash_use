/**
 * fastflash_sim_port.c - fast_flashdb_table 对接本平台模拟基座的移植层
 *
 * 本文件属于"框架适配层"（与 easyflash/ef_port.c、flashdb/fal_flash_sim_port.c
 * 同一定位），把 fast_flashdb_table 的 flash_ops_t 移植接口对接到
 * simulator/flash_sim.c 实现的统一 Flash 抽象（NOR/NAND/EEPROM 物理特性仿真）。
 *
 * 移植层本身不内含任何业务逻辑，仅做"组件接口 <-> 模拟基座"的参数与句柄桥接：
 *   flash_ops_t.init   -> 建立/恢复一个全局 sim 设备句柄（首次 init 时构造）
 *   flash_ops_t.read   -> flash_sim_read
 *   flash_ops_t.write  -> flash_sim_write
 *   flash_ops_t.erase  -> flash_sim_erase
 *
 * 模拟基座本身已承担：按块擦除、写入仅允许 1->0（NOR/NAND）、EEPROM 字节写、
 * 寿命统计、坏块模拟等物理特性，因此移植层无需再重复实现这些语义。
 */

#include "fast_flash_types.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 全局模拟设备句柄（由 fast_flash_sim_init_device 建立） */
static flash_dev_t *g_sim_dev = NULL;

/**
 * 由测试程序在 fast_flash_init 之前调用：按环境变量配置并打开模拟基座。
 * 环境变量与本项目其它框架一致：SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_WRITE/
 * SIM_CYCLES/SIM_RD_US/SIM_WR_US/SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R。
 * 返回 0 表示成功。
 */
int fast_flash_sim_init_device(const char *bin_path)
{
    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.bin_path = bin_path ? bin_path : "fastflash_sim.bin";
    FLASH_CFG_DEFAULTS_BY_TYPE(cfg, FLASH_TYPE_NOR);

    /* 覆盖来自环境变量的显式配置（与 kv/easyflash/flashdb 测试程序一致） */
    const char *v;
#define ENV_LONG(K, D) (((v) = getenv(K)) && *v ? (uint32_t)atol(v) : (D))
    cfg.type        = (flash_type_t)ENV_LONG("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size  = ENV_LONG("SIM_TOTAL", 64 * 1024);
    cfg.erase_size  = ENV_LONG("SIM_ERASE", 4096);
    cfg.write_size  = ENV_LONG("SIM_WRITE", 1);
    cfg.erase_cycles= ENV_LONG("SIM_CYCLES", 100000);
    cfg.read_us     = ENV_LONG("SIM_RD_US", 0);
    cfg.write_us    = ENV_LONG("SIM_WR_US", 0);
    cfg.erase_us    = ENV_LONG("SIM_ERASE_US", 0);
    cfg.bad_blocks  = ENV_LONG("SIM_BAD_N", 0);
    cfg.bad_ratio   = ENV_LONG("SIM_BAD_R", 0);
#undef ENV_LONG

    if (g_sim_dev) { flash_sim_deinit(g_sim_dev); g_sim_dev = NULL; }
    g_sim_dev = flash_sim_init(&cfg);
    return g_sim_dev ? 0 : -1;
}

void fast_flash_sim_deinit_device(void)
{
    if (g_sim_dev) { flash_sim_deinit(g_sim_dev); g_sim_dev = NULL; }
}

flash_dev_t *fast_flash_sim_device(void)
{
    return g_sim_dev;
}

/* ---- flash_ops_t 实现：桥接到模拟基座 ---- */

static int port_init(void)
{
    /* 设备句柄已在 fast_flash_sim_init_device 中建立；此处仅做有效性检查 */
    if (!g_sim_dev) return -1;
    return 0;
}

static int port_read(uint32_t addr, uint8_t *buf, uint32_t size)
{
    if (!g_sim_dev) return -1;
    return (flash_sim_read(g_sim_dev, addr, buf, size) == FLASH_OK) ? 0 : -1;
}

static int port_write(uint32_t addr, const uint8_t *buf, uint32_t size)
{
    if (!g_sim_dev) return -1;
    return (flash_sim_write(g_sim_dev, addr, buf, size) == FLASH_OK) ? 0 : -1;
}

static int port_erase(uint32_t addr, uint32_t size)
{
    if (!g_sim_dev) return -1;
    return (flash_sim_erase(g_sim_dev, addr, size) == FLASH_OK) ? 0 : -1;
}

/* 供测试程序/组件引用的全局 ops 实例 */
const flash_ops_t sim_flash_ops = {
    .init  = port_init,
    .read  = port_read,
    .write = port_write,
    .erase = port_erase,
};
