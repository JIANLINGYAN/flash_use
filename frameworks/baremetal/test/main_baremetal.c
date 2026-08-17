/**
 * main_baremetal.c - 裸机结构体配置框架（A/B 双备份 + CRC）运行验证
 *
 * 验证要点：
 *   write_read  结构体整块保存/读取
 *   update      多次更新后读到最新值
 *   ab_rotate   A/B 分区交替使用（磨损分摊）
 *   powerloss   写入途中掉电后仍能回退到上一份有效配置
 *   corrupt     单分区被破坏（模拟坏点）时自动从另一分区恢复
 *   factory     恢复出厂后读不到配置
 *   func        高频保存压测（统计磨损与数据丢失）
 *
 * 测试驱动（环境变量）：
 *   SIM_*        模拟基座参数
 *   BM_ROUNDS    func 压测保存次数（默认 200）
 *   KV_TESTS     启用的测试项（逗号分隔），复用平台统一的测试项机制
 *
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...
 */

#include "bm_config.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM_BIN "baremetal_demo.bin"

static int g_fail = 0;

static void expect(const char *name, int cond)
{
    printf("  [%s] %s\n", cond ? "OK  " : "FAIL", name);
    if (!cond) { g_fail++; }
}

static long env_long(const char *k, long def)
{
    const char *v = getenv(k);
    return (v && *v) ? atol(v) : def;
}

static int has_test(const char *tests, const char *name)
{
    if (!tests || !*tests) { return 1; }
    const char *p = tests;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == strlen(name) && strncmp(p, name, len) == 0) { return 1; }
        if (!comma) { break; }
        p = comma + 1;
    }
    return 0;
}

/*
 * 应用配置结构体：这正是"裸机结构体框架"的典型用法——
 * 直接把一个业务结构体整块存到 Flash，无需序列化。
 */
typedef struct {
    uint32_t magic_app;    /* 应用自定义标识 */
    uint16_t volume;       /* 音量 0~100 */
    uint16_t brightness;   /* 亮度 0~100 */
    uint8_t  language;     /* 语言编号 */
    uint8_t  bt_enable;    /* 蓝牙开关 */
    int16_t  calib_offset; /* 校准偏移 */
    char     device_name[16];
    uint32_t boot_count;   /* 开机次数（高频更新字段） */
} app_config_t;

static void fill_default(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic_app = 0xA5A5A5A5u;
    c->volume = 60;
    c->brightness = 80;
    c->language = 1;
    c->bt_enable = 1;
    c->calib_offset = -12;
    snprintf(c->device_name, sizeof(c->device_name), "flash-demo");
    c->boot_count = 1;
}

int main(void)
{
    printf("=== 裸机结构体配置框架（A/B 双备份 + CRC）运行验证 ===\n");

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    cfg.erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024);
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.read_size = 1;
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = BM_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);

    /* 裸机双备份依赖块擦除；EEPROM 无擦除概念，自动切到 NOR */
    if (cfg.type == FLASH_TYPE_EEPROM) {
        printf("  [info] 本框架依赖块擦除语义，EEPROM 模式自动切换为 NOR\n");
        cfg.type = FLASH_TYPE_NOR;
    }

    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) {
        printf("  Flash 初始化失败!\n");
        return 1;
    }

    /*
     * 分区规划：A/B 各占一个擦除块，紧邻放置（真实工程中通常放在
     * Flash 尾部两个扇区，由链接脚本或分区表固定地址）。
     */
    uint32_t part_size = cfg.erase_size;
    uint32_t base_a = 0;
    uint32_t base_b = cfg.erase_size;

    printf("  [info] 介质=%s 容量=%uB 块=%uB | A@0x%X B@0x%X 各 %uB"
           " | 配置体 %zuB\n",
           cfg.type == FLASH_TYPE_NAND ? "NAND" : "NOR",
           cfg.total_size, cfg.erase_size, base_a, base_b, part_size,
           sizeof(app_config_t));

    bm_config_t ctx;
    app_config_t cur, back;
    const char *tests = getenv("KV_TESTS");

    /* 初始状态：擦除两分区，模拟出厂空白 Flash */
    flash_sim_erase(dev, base_a, part_size * 2u);

    flash_err_t rc = bm_config_init(&ctx, dev, base_a, base_b, part_size,
                                    (uint32_t)sizeof(app_config_t));
    if (rc != FLASH_OK) {
        printf("  [FAIL] bm_config_init 失败 (rc=%d)\n", rc);
        flash_sim_deinit(dev);
        return 1;
    }
    printf("  [OK  ] bm_config_init 初始化成功\n");

    /* 首次上电：应读不到配置 */
    if (has_test(tests, "write_read")) {
        printf("\n[测试项] 首次上电 + 基础保存/读取\n");
        expect("空白 Flash 读不到配置",
               bm_config_load(&ctx, &back) == FLASH_ERR_ARGS);

        fill_default(&cur);
        expect("保存默认配置", bm_config_save(&ctx, &cur) == FLASH_OK);

        memset(&back, 0, sizeof(back));
        expect("读取配置", bm_config_load(&ctx, &back) == FLASH_OK);
        expect("结构体内容一致",
               memcmp(&cur, &back, sizeof(cur)) == 0);
    }

    /* 更新覆盖 */
    if (has_test(tests, "update")) {
        printf("\n[测试项] 更新覆盖\n");
        fill_default(&cur);
        bm_config_save(&ctx, &cur);

        cur.volume = 33;
        cur.boot_count = 12345;
        snprintf(cur.device_name, sizeof(cur.device_name), "renamed");
        expect("保存更新", bm_config_save(&ctx, &cur) == FLASH_OK);

        memset(&back, 0, sizeof(back));
        bm_config_load(&ctx, &back);
        expect("读到最新值",
               back.volume == 33 && back.boot_count == 12345
               && strcmp(back.device_name, "renamed") == 0);
    }

    /* A/B 轮换验证 */
    if (has_test(tests, "ab_rotate")) {
        printf("\n[测试项] A/B 分区交替（磨损分摊）\n");
        bm_config_reset(&ctx);
        fill_default(&cur);

        int8_t seen_a = 0, seen_b = 0;
        for (uint32_t i = 0; i < 6; i++) {
            cur.boot_count = i;
            bm_config_save(&ctx, &cur);
            if (ctx.cur_part == 0) { seen_a = 1; }
            if (ctx.cur_part == 1) { seen_b = 1; }
        }
        expect("A、B 两个分区都被使用过", seen_a && seen_b);

        bm_status_t st;
        bm_config_status(&ctx, &st);
        printf("    [info] A 有效=%d(seq=%u) B 有效=%d(seq=%u) 当前=%c\n",
               st.a_valid, st.a_seq, st.b_valid, st.b_seq,
               st.active == 0 ? 'A' : (st.active == 1 ? 'B' : '-'));
        expect("两分区均有效（互为备份）", st.a_valid && st.b_valid);

        uint32_t ca = 0, cb = 0;
        flash_sim_get_erase_count(dev, base_a, &ca);
        flash_sim_get_erase_count(dev, base_b, &cb);
        printf("    [info] 擦写次数 A=%u B=%u（应大致均摊）\n", ca, cb);
        expect("擦写次数分摊到两块", ca > 0 && cb > 0);
    }

    /*
     * 掉电安全：模拟"保存到目标分区途中掉电"。
     * 手法：先存好一份有效配置（落在某分区），然后擦除另一分区并只写入
     * 部分 payload（不写头部），完全等价于写入过程中断电的介质状态。
     * 预期：重新初始化后仍能读到上一份完好配置。
     */
    if (has_test(tests, "powerloss")) {
        printf("\n[测试项] 掉电安全（写入中断后回退）\n");
        bm_config_reset(&ctx);

        fill_default(&cur);
        cur.boot_count = 777;
        bm_config_save(&ctx, &cur);
        int8_t good_part = ctx.cur_part;

        /* 模拟对另一分区的写入中途掉电 */
        uint32_t victim = (good_part == 0) ? base_b : base_a;
        flash_sim_erase(dev, victim, part_size);
        uint8_t partial[32];
        memset(partial, 0x5A, sizeof(partial));
        flash_sim_write(dev, victim + sizeof(bm_header_t), partial,
                        sizeof(partial));

        /* 重新上电扫描 */
        bm_config_t ctx2;
        bm_config_init(&ctx2, dev, base_a, base_b, part_size,
                       (uint32_t)sizeof(app_config_t));
        memset(&back, 0, sizeof(back));
        expect("掉电后仍能读到配置",
               bm_config_load(&ctx2, &back) == FLASH_OK);
        expect("读到的是掉电前那份完好配置", back.boot_count == 777);

        bm_status_t st;
        bm_config_status(&ctx2, &st);
        expect("未完成写入的分区被判为无效",
               (victim == base_a) ? !st.a_valid : !st.b_valid);
    }

    /*
     * 单分区损坏恢复：直接篡改当前生效分区的 payload（模拟运行期坏点/位翻转），
     * CRC 校验会失败，框架应自动回退到另一个分区。
     */
    if (has_test(tests, "corrupt")) {
        printf("\n[测试项] 单分区数据损坏（CRC 失败）自动恢复\n");
        bm_config_reset(&ctx);

        fill_default(&cur);
        cur.boot_count = 1000;
        bm_config_save(&ctx, &cur);   /* 第一份 -> A */
        cur.boot_count = 2000;
        bm_config_save(&ctx, &cur);   /* 第二份 -> B（最新） */

        /* 破坏当前生效分区的数据区（把若干位由 1 编程为 0） */
        uint32_t active_base = (ctx.cur_part == 0) ? base_a : base_b;
        uint8_t zero[8];
        memset(zero, 0x00, sizeof(zero));
        flash_sim_write(dev, active_base + sizeof(bm_header_t) + 4, zero,
                        sizeof(zero));

        bm_config_t ctx3;
        bm_config_init(&ctx3, dev, base_a, base_b, part_size,
                       (uint32_t)sizeof(app_config_t));
        memset(&back, 0, sizeof(back));
        flash_err_t lr = bm_config_load(&ctx3, &back);
        expect("损坏后仍能加载（回退到备份）", lr == FLASH_OK);
        expect("回退到较旧但完好的那份", back.boot_count == 1000);
    }

    /* 恢复出厂 */
    if (has_test(tests, "factory")) {
        printf("\n[测试项] 恢复出厂设置\n");
        fill_default(&cur);
        bm_config_save(&ctx, &cur);
        expect("擦除双分区", bm_config_reset(&ctx) == FLASH_OK);
        expect("恢复出厂后读不到配置",
               bm_config_load(&ctx, &back) == FLASH_ERR_ARGS);
    }

    /* 高频保存压测 */
    uint32_t saves = 0, lost = 0;
    if (has_test(tests, "func")) {
        printf("\n[测试项] 高频保存压测\n");
        bm_config_reset(&ctx);
        fill_default(&cur);

        uint32_t rounds = (uint32_t)env_long("BM_ROUNDS", 200);
        for (uint32_t i = 0; i < rounds; i++) {
            cur.boot_count = i;
            cur.volume = (uint16_t)(i % 101u);
            if (bm_config_save(&ctx, &cur) != FLASH_OK) { lost++; continue; }
            saves++;

            memset(&back, 0, sizeof(back));
            if (bm_config_load(&ctx, &back) != FLASH_OK
                || back.boot_count != i
                || back.volume != (uint16_t)(i % 101u)) {
                lost++;
            }
        }
        printf("    [info] 保存 %u 次，数据丢失 %u 次\n", saves, lost);
        expect("压测无数据丢失", lost == 0);

        uint32_t ca = 0, cb = 0;
        flash_sim_get_erase_count(dev, base_a, &ca);
        flash_sim_get_erase_count(dev, base_b, &cb);
        printf("    [info] 擦写次数 A=%u B=%u\n", ca, cb);

        flash_stats_t fst;
        flash_sim_get_stats(dev, &fst);
        printf("STATS_JSON:{\"mode\":\"func\",\"ops\":%u,\"lost\":%u,"
               "\"block_us\":%llu,\"reads\":%u,\"writes\":%u,\"erases\":%u,"
               "\"max_cycles\":%u,\"avg_cycles\":%u,\"erase_cycles\":%u}\n",
               saves, lost,
               (unsigned long long)(fst.read_time_us + fst.write_time_us
                                    + fst.erase_time_us),
               fst.total_reads, fst.total_writes, fst.total_erases,
               fst.max_erase_cycles, fst.avg_erase_cycles, cfg.erase_cycles);
    } else {
        flash_stats_t st;
        flash_sim_get_stats(dev, &st);
        printf("STATS_JSON:{\"mode\":\"basic\",\"reads\":%u,\"writes\":%u,"
               "\"erases\":%u,\"write_bytes\":%u,\"max_cycles\":%u,"
               "\"avg_cycles\":%u,\"read_us\":%llu,\"write_us\":%llu,"
               "\"erase_us\":%llu,\"bad_blocks\":%u,\"erase_cycles\":%u}\n",
               st.total_reads, st.total_writes, st.total_erases,
               st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
               (unsigned long long)st.read_time_us,
               (unsigned long long)st.write_time_us,
               (unsigned long long)st.erase_time_us, st.bad_block_count,
               cfg.erase_cycles);
    }

    uint32_t bc = flash_sim_block_count(dev);
    if (bc > 0) {
        uint32_t *map = (uint32_t *)malloc(sizeof(uint32_t) * bc);
        if (map) {
            flash_sim_get_wear_map(dev, map, bc);
            printf("WEARMAP:");
            for (uint32_t i = 0; i < bc; i++) {
                printf("%s%u", i ? "," : "", map[i]);
            }
            printf("\n");
            free(map);
        }
    }

    flash_sim_deinit(dev);
    printf("\n=== 裸机框架运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
