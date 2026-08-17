/**
 * main_sim.c - 模拟基座示例 / 自检程序
 *
 * 支持通过环境变量配置介质参数（前端注入）：
 *   SIM_TYPE(0=NOR,1=NAND,2=EEPROM) SIM_TOTAL SIM_ERASE SIM_WRITE SIM_RD_US
 *   SIM_WR_US SIM_ERASE_US SIM_CYCLES SIM_BAD_N SIM_BAD_R
 * 无环境变量时使用默认 64KB NOR 配置。
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

int main(void)
{
    int fails = 0;
    printf("=== Flash 模拟基座自检 ===\n");

    /* ---------- NOR Flash 示例（配置可经 env 覆盖） ---------- */
    printf("\n[NOR Flash]\n");
    flash_config_t cfg = {
        .type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR),
        .total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024),
        .erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024),
        .write_size = (uint32_t)env_long("SIM_WRITE", 1),
        .read_size = 1,
        .erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000),
        .bin_path = NOR_BIN,
        .read_us = (uint32_t)env_long("SIM_RD_US", 0),
        .write_us = (uint32_t)env_long("SIM_WR_US", 0),
        .erase_us = (uint32_t)env_long("SIM_ERASE_US", 0),
        .bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0),
        .bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0),
    };
    flash_dev_t *nor = flash_sim_init(&cfg);
    if (!nor) { printf("  NOR init failed!\n"); return 1; }

    uint8_t wbuf[16];
    for (int i = 0; i < 16; i++) wbuf[i] = (uint8_t)(0xA0 + i);
    uint8_t rbuf[16] = {0};

    fails += check("erase block 0",
                   flash_sim_erase(nor, 0, cfg.erase_size), FLASH_OK);
    fails += check("write after erase",
                   flash_sim_write(nor, 0, wbuf, sizeof(wbuf)), FLASH_OK);
    fails += check("read back",
                   flash_sim_read(nor, 0, rbuf, sizeof(rbuf)), FLASH_OK);
    fails += check("data match",
                   memcmp(wbuf, rbuf, sizeof(wbuf)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    /*
     * NOR 编程语义验证（按位与，只能 1->0）：
     *  1) 未擦除区域写入全 0xFF：真实 NOR 上等价于"不改变任何位"，属合法操作，
     *     且数据保持原值（不会被擦回 0xFF）。
     *  2) 未擦除区域继续把某些位由 1 编程为 0：合法，结果为原值 & 新值。
     * 注意：此处不再期望返回 FLASH_ERR_WRITE——旧断言把"写 0xFF"当作非法，
     * 与真实 NOR 不符，也会导致 EasyFlash/FlashDB 等成熟框架无法正常工作。
     */
    uint8_t ov[16];
    memset(ov, 0xFF, sizeof(ov));
    fails += check("overwrite 0xFF (no-op, allowed)",
                   flash_sim_write(nor, 0, ov, sizeof(ov)), FLASH_OK);
    uint8_t chk[16];
    flash_sim_read(nor, 0, chk, sizeof(chk));
    fails += check("overwrite 0xFF keeps data",
                   memcmp(chk, wbuf, sizeof(chk)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    uint8_t zero[16];
    memset(zero, 0x00, sizeof(zero));
    fails += check("program bits 1->0 (allowed)",
                   flash_sim_write(nor, 0, zero, sizeof(zero)), FLASH_OK);
    flash_sim_read(nor, 0, chk, sizeof(chk));
    uint8_t allzero = 0;
    for (size_t i = 0; i < sizeof(chk); i++) { allzero |= chk[i]; }
    fails += check("bits become 0x00",
                   allzero == 0 ? FLASH_OK : FLASH_ERR_IO, FLASH_OK);

    printf("  [info] 性能与磨损统计：\n");
    dump_stats(nor, cfg.erase_cycles);

    flash_sim_deinit(nor);

    /* ---------- EEPROM 示例（固定，演示字节写） ---------- */
    printf("\n[EEPROM]\n");
    flash_config_t ecfg = {
        .type = FLASH_TYPE_EEPROM,
        .total_size = 2 * 1024,
        .erase_size = 0,
        .write_size = 1,
        .read_size = 1,
        .erase_cycles = 0,
        .bin_path = EEPROM_BIN
    };
    flash_dev_t *eep = flash_sim_init(&ecfg);
    if (!eep) { printf("  EEPROM init failed!\n"); return 1; }

    uint8_t ew[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t er[8] = {0};
    fails += check("eeprom write",
                   flash_sim_write(eep, 100, ew, sizeof(ew)), FLASH_OK);
    fails += check("eeprom read",
                   flash_sim_read(eep, 100, er, sizeof(er)), FLASH_OK);
    fails += check("eeprom data match",
                   memcmp(ew, er, sizeof(ew)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);
    fails += check("eeprom erase (reject)",
                   flash_sim_erase(eep, 0, 1), FLASH_ERR_NOTSUP);

    flash_sim_deinit(eep);

    printf("\n=== 自检结果: %s ===\n", fails == 0 ? "全部通过" : "存在失败");
    return fails == 0 ? 0 : 1;
}
