/**
 * main_sim.c - 模拟基座示例 / 自检程序
 *
 * 支持通过环境变量配置介质参数（前端注入）：
 *   SIM_TYPE(0=NOR,1=NAND,2=EEPROM) SIM_TOTAL SIM_ERASE SIM_WRITE SIM_RD_US
 *   SIM_WR_US SIM_ERASE_US SIM_CYCLES SIM_BAD_N SIM_BAD_R
 * 未显式配置的项将按介质类型套用物理默认值（见 flash_sim.h 中的
 * FLASH_CFG_DEFAULTS_BY_TYPE）。
 *
 * 自检按介质类型分派：
 *  - NOR / NAND：验证"擦除 -> 写 -> 读 -> 位与编程"语义（NAND 额外验证坏块拒绝）。
 *  - EEPROM   ：验证"单字节原地改写、无需擦除、擦除被拒绝"语义。
 *
 * 自检通过后输出（供后端解析）：
 *   STATS_JSON:{...}   性能与磨损统计
 *   WEARMAP:comma,list 每块擦写次数（仅 NOR/NAND）
 */

#include "flash_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOR_BIN      "nor_demo.bin"
#define NAND_BIN     "nand_demo.bin"
#define EEPROM_BIN   "eeprom_demo.bin"

static int check(const char *name, flash_err_t rc, flash_err_t expect)
{
    if (rc == expect) {
        printf("  [OK]   %-32s rc=%d\n", name, rc);
        return 0;
    }
    printf("  [FAIL] %-32s rc=%d (expect %d)\n", name, rc, expect);
    return 1;
}

static long env_long(const char *k, long def)
{
    const char *v = getenv(k);
    if (v && *v) { return atol(v); }
    return def;
}

/* 输出统计与磨损图（后端按前缀解析） */
static void dump_stats(flash_dev_t *dev, uint32_t erase_cycles)
{
    flash_stats_t st;
    flash_sim_get_stats(dev, &st);
    printf("STATS_JSON:{\"reads\":%u,\"writes\":%u,\"erases\":%u,"
           "\"write_bytes\":%u,\"max_cycles\":%u,\"avg_cycles\":%u,"
           "\"read_us\":%llu,\"write_us\":%llu,\"erase_us\":%llu,"
           "\"bad_blocks\":%u,\"erase_cycles\":%u}\n",
           st.total_reads, st.total_writes, st.total_erases,
           st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
           (unsigned long long)st.read_time_us,
           (unsigned long long)st.write_time_us,
           (unsigned long long)st.erase_time_us,
           st.bad_block_count, erase_cycles);

    uint32_t n = flash_sim_block_count(dev);
    if (n > 0) {
        uint32_t *map = (uint32_t *)malloc(sizeof(uint32_t) * n);
        if (map) {
            flash_sim_get_wear_map(dev, map, n);
            printf("WEARMAP:");
            for (uint32_t i = 0; i < n; i++) {
                printf("%s%u", i ? "," : "", map[i]);
            }
            printf("\n");
            free(map);
        }
    }
}

/* 构建带类型默认值的配置（未显式配置项走默认值宏） */
static flash_config_t build_cfg(flash_type_t type, const char *bin)
{
    flash_config_t cfg = {0};
    cfg.type = type;
    cfg.total_size   = (uint32_t)env_long("SIM_TOTAL", 0);
    cfg.erase_size   = (uint32_t)env_long("SIM_ERASE", 0);
    cfg.write_size   = (uint32_t)env_long("SIM_WRITE", 0);
    cfg.read_size    = (uint32_t)env_long("SIM_READ", 0);
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 0);
    cfg.bin_path = bin;
    cfg.read_us  = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio  = (uint32_t)env_long("SIM_BAD_R", 0);
    FLASH_CFG_DEFAULTS_BY_TYPE(cfg, type);
    return cfg;
}

/*
 * NOR / NAND 通用自检：验证"擦除 -> 写 -> 读 -> 位与编程"语义。
 * NAND 额外验证：坏块擦除被拒绝（使用 bad_blocks 触发）。
 */
static int self_test_block(flash_dev_t *dev, const flash_config_t *cfg)
{
    int fails = 0;
    uint8_t wbuf[16];
    for (int i = 0; i < 16; i++) wbuf[i] = (uint8_t)(0xA0 + i);
    uint8_t rbuf[16] = {0};

    fails += check("erase block 0",
                   flash_sim_erase(dev, 0, cfg->erase_size), FLASH_OK);
    fails += check("write after erase",
                   flash_sim_write(dev, 0, wbuf, sizeof(wbuf)), FLASH_OK);
    fails += check("read back",
                   flash_sim_read(dev, 0, rbuf, sizeof(rbuf)), FLASH_OK);
    fails += check("data match",
                   memcmp(wbuf, rbuf, sizeof(wbuf)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    /*
     * 编程语义验证（按位与，只能 1->0）：
     *  1) 未擦除区域写入全 0xFF：等价"不改变任何位"，合法，数据保持原值。
     *  2) 未擦除区域继续把某些位由 1 编程为 0：合法，结果为原值 & 新值。
     */
    uint8_t ov[16];
    memset(ov, 0xFF, sizeof(ov));
    fails += check("overwrite 0xFF (no-op, allowed)",
                   flash_sim_write(dev, 0, ov, sizeof(ov)), FLASH_OK);
    uint8_t chk[16];
    flash_sim_read(dev, 0, chk, sizeof(chk));
    fails += check("overwrite 0xFF keeps data",
                   memcmp(chk, wbuf, sizeof(chk)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    uint8_t zero[16];
    memset(zero, 0x00, sizeof(zero));
    fails += check("program bits 1->0 (allowed)",
                   flash_sim_write(dev, 0, zero, sizeof(zero)), FLASH_OK);
    flash_sim_read(dev, 0, chk, sizeof(chk));
    uint8_t allzero = 0;
    for (size_t i = 0; i < sizeof(chk); i++) { allzero |= chk[i]; }
    fails += check("bits become 0x00",
                   allzero == 0 ? FLASH_OK : FLASH_ERR_IO, FLASH_OK);

    /* NAND 特有：坏块擦除必须被拒绝 */
    if (cfg->type == FLASH_TYPE_NAND && cfg->bad_blocks > 0) {
        uint32_t blk_idx = 0;
        int found = 0;
        for (uint32_t b = 0; b < cfg->total_size; b += cfg->erase_size) {
            uint32_t cnt = 0;
            if (flash_sim_get_erase_count(dev, b, &cnt) == FLASH_OK
                && cnt >= cfg->erase_cycles) {
                blk_idx = b;
                found = 1;
                break;
            }
        }
        if (found) {
            fails += check("erase bad block (reject)",
                           flash_sim_erase(dev, blk_idx, cfg->erase_size),
                           FLASH_ERR_ERASE);
        }
    }

    printf("  [info] 性能与磨损统计：\n");
    dump_stats(dev, cfg->erase_cycles);
    return fails;
}

/*
 * EEPROM 自检：验证"单字节原地改写、无需擦除、擦除被拒绝"。
 */
static int self_test_eeprom(flash_dev_t *dev, const flash_config_t *cfg)
{
    int fails = 0;
    uint8_t ew[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t er[8] = {0};

    fails += check("eeprom write",
                   flash_sim_write(dev, 100, ew, sizeof(ew)), FLASH_OK);
    fails += check("eeprom read",
                   flash_sim_read(dev, 100, er, sizeof(er)), FLASH_OK);
    fails += check("eeprom data match",
                   memcmp(ew, er, sizeof(ew)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    /* EEPROM 单字节原地改写：不擦除直接覆盖，新值应覆盖旧值 */
    uint8_t ew2[8] = {11, 12, 13, 14, 15, 16, 17, 18};
    uint8_t er2[8] = {0};
    fails += check("eeprom overwrite in-place",
                   flash_sim_write(dev, 100, ew2, sizeof(ew2)), FLASH_OK);
    flash_sim_read(dev, 100, er2, sizeof(er2));
    fails += check("eeprom overwrite match",
                   memcmp(ew2, er2, sizeof(ew2)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    fails += check("eeprom erase (reject)",
                   flash_sim_erase(dev, 0, 1), FLASH_ERR_NOTSUP);
    fails += check("eeprom get_erase_count (reject)",
                   flash_sim_get_erase_count(dev, 0, &(uint32_t){0}),
                   FLASH_ERR_NOTSUP);

    printf("  [info] 性能与磨损统计：\n");
    dump_stats(dev, cfg->erase_cycles);
    return fails;
}

int main(void)
{
    int fails = 0;
    flash_type_t type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    const char *name = (type == FLASH_TYPE_NAND) ? "NAND Flash"
                      : (type == FLASH_TYPE_EEPROM) ? "EEPROM"
                      : "NOR Flash";
    printf("=== Flash 模拟基座自检 [%s] ===\n", name);

    if (type == FLASH_TYPE_EEPROM) {
        flash_config_t cfg = build_cfg(FLASH_TYPE_EEPROM, EEPROM_BIN);
        printf("\n[EEPROM]\n");
        flash_dev_t *dev = flash_sim_init(&cfg);
        if (!dev) { printf("  EEPROM init failed!\n"); return 1; }
        fails += self_test_eeprom(dev, &cfg);
        flash_sim_deinit(dev);
    } else {
        const char *bin = (type == FLASH_TYPE_NAND) ? NAND_BIN : NOR_BIN;
        flash_config_t cfg = build_cfg(type, bin);
        printf("\n[%s]\n", name);
        flash_dev_t *dev = flash_sim_init(&cfg);
        if (!dev) { printf("  %s init failed!\n", name); return 1; }
        fails += self_test_block(dev, &cfg);
        flash_sim_deinit(dev);
    }

    printf("\n=== 自检结果: %s ===\n", fails == 0 ? "全部通过" : "存在失败");
    return fails == 0 ? 0 : 1;
}
