/**
 * main_sim.c - 模拟基座示例 / 自检程序（第一步完成标准验证）
 *
 * 行为：
 *  1. 创建一个 NOR Flash 模拟设备（BIN 文件 nor_demo.bin）
 *  2. 擦除一个块 -> 写入一段数据 -> 读取回来 -> 校验一致
 *  3. 演示"未擦除就写"会被拒绝（符合 NOR 物理特性）
 *  4. 创建一个 EEPROM 模拟设备（BIN 文件 eeprom_demo.bin）
 *  5. 直接字节级写入并读取校验
 *
 * 运行后可用十六进制工具打开 nor_demo.bin / eeprom_demo.bin 自行检验落盘内容。
 */

#include "flash_sim.h"
#include <stdio.h>
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

int main(void)
{
    int fails = 0;
    printf("=== Flash 模拟基座自检 ===\n");

    /* ---------- NOR Flash 示例 ---------- */
    printf("\n[NOR Flash]\n");
    flash_config_t cfg = {
        .type = FLASH_TYPE_NOR,
        .total_size = 64 * 1024,     /* 64KB */
        .erase_size = 4 * 1024,      /* 4KB/块 */
        .write_size = 1,             /* 最小写入 1 字节（演示用） */
        .read_size = 1,
        .erase_cycles = 100000,
        .bin_path = NOR_BIN
    };
    flash_dev_t *nor = flash_sim_init(&cfg);
    if (!nor) { printf("  NOR init failed!\n"); return 1; }

    uint8_t wbuf[16];
    for (int i = 0; i < 16; i++) wbuf[i] = (uint8_t)(0xA0 + i);
    uint8_t rbuf[16] = {0};

    /* 擦除块 0 */
    fails += check("erase block 0",
                   flash_sim_erase(nor, 0, cfg.erase_size), FLASH_OK);

    /* 写入并读取校验 */
    fails += check("write after erase",
                   flash_sim_write(nor, 0, wbuf, sizeof(wbuf)), FLASH_OK);
    fails += check("read back",
                   flash_sim_read(nor, 0, rbuf, sizeof(rbuf)), FLASH_OK);
    fails += check("data match",
                   memcmp(wbuf, rbuf, sizeof(wbuf)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    /* 未擦除的覆盖写：已写 0xA0 后试图写 0xFF（需把已为 0 的位翻回 1），
     * 违反 NOR 只能 1->0 的物理特性，应被拒绝 */
    uint8_t ov[16];
    memset(ov, 0xFF, sizeof(ov));
    fails += check("overwrite without erase (reject)",
                   flash_sim_write(nor, 0, ov, sizeof(ov)),
                   FLASH_ERR_WRITE);

    /* 统计信息 */
    flash_stats_t st;
    flash_sim_get_stats(nor, &st);
    printf("  stats: reads=%u writes=%u erases=%u max_cycles=%u\n",
           st.total_reads, st.total_writes, st.total_erases, st.max_erase_cycles);

    uint32_t cyc = 0;
    flash_sim_get_erase_count(nor, 0, &cyc);
    printf("  block0 erase count = %u\n", cyc);

    flash_sim_deinit(nor);

    /* ---------- EEPROM 示例 ---------- */
    printf("\n[EEPROM]\n");
    flash_config_t ecfg = {
        .type = FLASH_TYPE_EEPROM,
        .total_size = 2 * 1024,      /* 2KB */
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
    /* EEPROM 不支持 erase */
    fails += check("eeprom erase (reject)",
                   flash_sim_erase(eep, 0, 1), FLASH_ERR_NOTSUP);

    flash_sim_deinit(eep);

    printf("\n=== 自检结果: %s ===\n", fails == 0 ? "全部通过" : "存在失败");
    return fails == 0 ? 0 : 1;
}
