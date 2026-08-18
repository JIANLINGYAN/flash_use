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

/*
 * 测试组件日志（仅测试程序使用，不污染 flash_sim 框架本身）。
 * 级别前缀与后端 parse_output / _classify_line 约定一致：
 *   [INFO] 普通信息  [OK] 通过  [FAIL] 失败  [WARN] 警告
 * STATS_JSON:/WEARMAP: 为后端专用数据行，不含级别前缀。
 */
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)

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
    LOG_INFO("阶段1: 擦除块0 -> 写 -> 读 -> 数据比对");


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
        LOG_INFO("阶段3: 坏块擦除拒绝验证");
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

    LOG_INFO("阶段4: 输出性能与磨损统计");
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
    LOG_INFO("阶段1: 写 -> 读 -> 数据比对（验证无需擦除）");

    fails += check("eeprom write",
                   flash_sim_write(dev, 100, ew, sizeof(ew)), FLASH_OK);
    fails += check("eeprom read",
                   flash_sim_read(dev, 100, er, sizeof(er)), FLASH_OK);
    fails += check("eeprom data match",
                   memcmp(ew, er, sizeof(ew)) == 0 ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    /* EEPROM 单字节原地改写：不擦除直接覆盖，新值应覆盖旧值 */
    LOG_INFO("阶段2: 单字节原地改写（不擦除直接覆盖）");
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

    LOG_INFO("阶段3: 输出性能与磨损统计");
    printf("  [info] 性能与磨损统计：\n");
    dump_stats(dev, cfg->erase_cycles);
    return fails;
}

/*
 * 通用测试项: badblock（仅 NAND 有意义）
 * 验证两类坏块擦除拒绝：
 *   1) 固定坏块（SIM_BAD_N>0 时初始化随机标记）——擦除被拒绝。
 *   2) 寿命耗尽块（擦满 erase_cycles 后）——擦除被拒绝。
 * 需要较小的 erase_cycles 以便快速写满触发。
 */
static int self_test_badblock(flash_dev_t *dev, const flash_config_t *cfg)
{
    int fails = 0;
    if (cfg->type != FLASH_TYPE_NAND) {
        printf("  [SKIP] badblock 仅适用于 NAND，当前类型跳过\n");
        return 0;
    }
    LOG_INFO("通用测试 badblock: 触发并验证坏块擦除拒绝");

    /* 先写满所有块的擦除寿命，使其寿命耗尽退化成坏块 */
    uint32_t nblocks = cfg->total_size / cfg->erase_size;
    for (uint32_t b = 0; b < nblocks; b++) {
        uint32_t off = b * cfg->erase_size;
        for (uint32_t c = 0; c < cfg->erase_cycles + 1; c++) {
            if (flash_sim_erase(dev, off, cfg->erase_size) != FLASH_OK) break;
        }
    }

    /* 找到一个寿命耗尽块，验证后续擦除被拒绝 */
    int found = 0;
    for (uint32_t b = 0; b < nblocks; b++) {
        uint32_t off = b * cfg->erase_size;
        uint32_t cnt = 0;
        if (flash_sim_get_erase_count(dev, off, &cnt) == FLASH_OK
            && cnt >= cfg->erase_cycles) {
            fails += check("worn-out block erase (reject)",
                           flash_sim_erase(dev, off, cfg->erase_size),
                           FLASH_ERR_ERASE);
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("  [WARN] 未触发寿命耗尽块（可能寿命/容量配置过小）\n");
    }
    return fails;
}

/*
 * 通用测试项: wear（磨损统计，仅 NOR/NAND）
 * 对多个不同块执行多轮擦写，验证磨损图（WEARMAP）对应块计数递增
 * 且最大擦除次数（max_cycles）随之增长。
 */
static int self_test_wear(flash_dev_t *dev, const flash_config_t *cfg)
{
    int fails = 0;
    uint32_t nblocks = cfg->total_size / cfg->erase_size;
    LOG_INFO("通用测试 wear: 对前 4 个块执行多轮擦写验证磨损统计");

    uint32_t before = 0;
    flash_sim_get_erase_count(dev, 0, &before);

    uint8_t wbuf[16];
    for (int i = 0; i < 16; i++) wbuf[i] = (uint8_t)(0xB0 + i);
    uint32_t rounds = 5;
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t b = 0; b < 4 && b < nblocks; b++) {
            uint32_t off = b * cfg->erase_size;
            if (flash_sim_erase(dev, off, cfg->erase_size) != FLASH_OK) continue;
            flash_sim_write(dev, off, wbuf, sizeof(wbuf));
        }
    }

    uint32_t after = 0;
    flash_sim_get_erase_count(dev, 0, &after);
    fails += check("wear count increased",
                   (after >= before + rounds) ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    flash_stats_t st;
    flash_sim_get_stats(dev, &st);
    fails += check("max_cycles grows",
                   (st.max_erase_cycles >= rounds) ? FLASH_OK : FLASH_ERR_IO,
                   FLASH_OK);

    printf("  [info] 磨损统计：\n");
    dump_stats(dev, cfg->erase_cycles);
    return fails;
}

/*
 * 通用测试项: powerloss（掉电重放）
 * 含义：写入数据后"断电"（释放设备但不擦介质），重新上电（用同一介质文件
 * 重新初始化）应能读回此前写入的数据，验证持久化语义。
 * 借鉴 fast_flashdb_table 的掉电重放测试项，下沉到模拟基座层面。
 * 使用独立介质文件并全程自管设备生命周期，避免与 run_selected 传入的
 * 主设备冲突（双重释放）。
 */
#define POWERLOSS_BIN "powerloss_demo.bin"
static int self_test_powerloss(const flash_config_t *cfg)
{
    int fails = 0;
    uint8_t wbuf[32];
    for (int i = 0; i < 32; i++) wbuf[i] = (uint8_t)(0x30 + i);
    LOG_INFO("通用测试 powerloss: 写入标记数据后模拟断电重放");

    /* 保证从干净介质开始：删除可能残留的旧介质文件 */
    remove(POWERLOSS_BIN);

    flash_config_t cfg1 = *cfg;
    cfg1.bin_path = POWERLOSS_BIN;
    flash_dev_t *dev = flash_sim_init(&cfg1);
    if (!dev) {
        printf("  [FAIL] powerloss init failed!\n");
        return 1;
    }
    fails += check("powerloss write before reset",
                   flash_sim_write(dev, 0, wbuf, sizeof(wbuf)), FLASH_OK);
    /* 断电：释放设备（介质文件保留） */
    flash_sim_deinit(dev);

    /* 重新上电：用同一介质文件重新初始化 */
    flash_config_t cfg2 = *cfg;
    cfg2.bin_path = POWERLOSS_BIN;
    flash_dev_t *dev2 = flash_sim_init(&cfg2);
    if (!dev2) {
        printf("  [FAIL] powerloss re-init failed!\n");
        return 1;
    }
    uint8_t rbuf[32] = {0};
    fails += check("powerloss read after reset",
                   flash_sim_read(dev2, 0, rbuf, sizeof(rbuf)), FLASH_OK);
    fails += check("powerloss data persisted",
                   memcmp(wbuf, rbuf, sizeof(wbuf)) == 0 ? FLASH_OK
                                                         : FLASH_ERR_IO,
                   FLASH_OK);
    dump_stats(dev2, cfg->erase_cycles);
    flash_sim_deinit(dev2);
    return fails;
}

/* 依据 SIM_TESTS 选择要执行的通用测试项；为空则执行全部 */
static int run_selected(flash_dev_t *dev, const flash_config_t *cfg,
                        const char *tests)
{
    int fails = 0;
    int run_any = 0;

    if (tests == NULL || *tests == '\0' || strstr(tests, "basic") != NULL) {
        printf("\n[通用测试: basic]\n");
        if (cfg->type == FLASH_TYPE_EEPROM)
            fails += self_test_eeprom(dev, cfg);
        else
            fails += self_test_block(dev, cfg);
        run_any = 1;
    }
    if (tests == NULL || *tests == '\0' || strstr(tests, "bad") != NULL) {
        printf("\n[通用测试: badblock]\n");
        fails += self_test_badblock(dev, cfg);
        run_any = 1;
    }
    if (tests == NULL || *tests == '\0' || strstr(tests, "wear") != NULL) {
        printf("\n[通用测试: wear]\n");
        if (cfg->type == FLASH_TYPE_EEPROM)
            printf("  [SKIP] wear 不适用于 EEPROM，跳过\n");
        else
            fails += self_test_wear(dev, cfg);
        run_any = 1;
    }
    if (tests == NULL || *tests == '\0' || strstr(tests, "powerloss") != NULL) {
        printf("\n[通用测试: powerloss]\n");
        fails += self_test_powerloss(cfg);
        run_any = 1;
    }
    if (!run_any) {
        printf("\n[WARN] 未匹配到任何测试项，默认执行 basic\n");
        if (cfg->type == FLASH_TYPE_EEPROM)
            fails += self_test_eeprom(dev, cfg);
        else
            fails += self_test_block(dev, cfg);
    }
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

    const char *tests = getenv("SIM_TESTS");
    if (tests && *tests) {
        LOG_INFO("启用的通用测试项: %s", tests);
    } else {
        LOG_INFO("未指定 SIM_TESTS，执行全部通用测试项");
    }

    const char *bin = (type == FLASH_TYPE_NAND) ? NAND_BIN
                    : (type == FLASH_TYPE_EEPROM) ? EEPROM_BIN
                    : NOR_BIN;
    flash_config_t cfg = build_cfg(type, bin);
    LOG_INFO("%s 自检: total=%u erase=%u write=%u endurance=%u bad=%u bad_r=%u",
             name, cfg.total_size, cfg.erase_size, cfg.write_size,
             cfg.erase_cycles, cfg.bad_blocks, cfg.bad_ratio);
    printf("\n[%s]\n", name);

    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) {
        printf("  [FAIL] %s init failed!\n", name);
        LOG_WARN("init 失败，请检查 bin 路径与容量配置");
        return 1;
    }
    LOG_INFO("%s 初始化完成，开始通用测试项验证", name);

    fails += run_selected(dev, &cfg, tests);

    flash_sim_deinit(dev);
    LOG_INFO("%s 自检结束，释放模拟设备", name);

    printf("\n=== 自检结果: %s ===\n", fails == 0 ? "全部通过" : "存在失败");
    LOG_INFO("自检完成: %s", fails == 0 ? "全部通过" : "存在失败");
    return fails == 0 ? 0 : 1;
}
