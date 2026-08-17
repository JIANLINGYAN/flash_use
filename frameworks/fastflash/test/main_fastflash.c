/**
 * main_fastflash.c - fast_flashdb_table 组件运行验证
 *
 * 本测试程序把 fast_flashdb_table 组件（vendor）通过本平台模拟基座
 * （frameworks/fastflash/fastflash_sim_port.c）运行起来，覆盖基础读写、
 * 追加、按索引覆盖、删除、垃圾回收与掉电重放等能力，并输出结构化统计
 * 供后端解析。
 *
 * 测试驱动（环境变量，与本平台其它框架一致）：
 *   SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_WRITE/SIM_CYCLES/SIM_RD_US/SIM_WR_US/
 *   SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R   模拟基座介质配置
 *   FLT_TESTS    启用的测试项（逗号分隔），不设置则全部运行：
 *                   init       初始化并创建表
 *                   write_read 基础写入/按索引读取
 *                   append     追加数据 + 计数
 *                   update     按索引覆盖写
 *                   delete     删除表
 *                   gc         垃圾回收
 *                   powerloss  掉电重放（重新初始化后数据仍在）
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...；级别前缀与
 * parse_output / _classify_line 约定一致：[INFO] [OK] [FAIL] [WARN]。
 */

#include "fast_flash_core.h"
#include "fast_flash_types.h"
#include "fastflash_sim_port.h"   /* 提供 sim_flash_ops 与设备初始化 */
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLT_BIN "fastflash_sim.bin"

/* 表名与单条记录结构 */
#define TBL_NAME "sensor_log"
typedef struct {
    uint32_t seq;
    int32_t  value;
    uint8_t  tag;
} sensor_rec_t;

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
    if (!tests || !*tests) return 1;
    const char *p = tests;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == strlen(name) && strncmp(p, name, len) == 0) return 1;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

/* 测试项：初始化并创建表 */
static void t_init(void)
{
    expect("init: create_table",
           fast_flash_create_table(TBL_NAME, sizeof(sensor_rec_t), 32) == 0 ||
           fast_flash_table_exists(TBL_NAME));
    flash_table_t info;
    expect("init: table_exists", fast_flash_table_exists(TBL_NAME) == true);
    if (fast_flash_get_table_info(TBL_NAME, &info) == 0) {
        expect("init: table_size valid", info.size > 0);
    } else {
        expect("init: get_table_info", 0);
    }
}

/* 测试项：基础写入 + 按索引读取 */
static void t_write_read(void)
{
    sensor_rec_t rec = { .seq = 1, .value = 12345, .tag = 0xA5 };
    /* 空表需先用顺序写入（追加）建立首条记录，再按索引读取 */
    expect("write_read: write index0",
           fast_flash_write_table_data(TBL_NAME, &rec, sizeof(rec)) == 0);
    sensor_rec_t rb = {0};
    expect("write_read: read index0",
           fast_flash_read_table_data(TBL_NAME, 0, &rb, sizeof(rb)) == 0);
    expect("write_read: match",
           rb.seq == 1 && rb.value == 12345 && rb.tag == 0xA5);
}

/* 测试项：追加数据 + 计数 */
static void t_append(void)
{
    /* 重建空表，保证计数独立可预期 */
    if (fast_flash_table_exists(TBL_NAME)) fast_flash_delete_table(TBL_NAME);
    fast_flash_create_table(TBL_NAME, sizeof(sensor_rec_t), 32);
    expect("append: append#1", fast_flash_append_table_data(TBL_NAME,
            &(sensor_rec_t){.seq=10,.value=111,.tag=1}, sizeof(sensor_rec_t)) == 0);
    expect("append: append#2", fast_flash_append_table_data(TBL_NAME,
            &(sensor_rec_t){.seq=11,.value=222,.tag=2}, sizeof(sensor_rec_t)) == 0);
    expect("append: count==2", fast_flash_get_table_count(TBL_NAME) == 2);
}

/* 测试项：按索引覆盖写 */
static void t_update(void)
{
    /* 先顺序写一条作为种子，再按索引覆盖 index 0 */
    sensor_rec_t seed = { .seq = 100, .value = 111, .tag = 0x11 };
    expect("update: seed", fast_flash_write_table_data(TBL_NAME, &seed, sizeof(seed)) == 0);
    sensor_rec_t nu = { .seq = 999, .value = -7, .tag = 0x3C };
    expect("update: overwrite index0",
           fast_flash_write_table_data_by_index(TBL_NAME, 0, &nu, sizeof(nu)) == 0);
    sensor_rec_t rb = {0};
    expect("update: read index0",
           fast_flash_read_table_data(TBL_NAME, 0, &rb, sizeof(rb)) == 0);
    expect("update: match", rb.seq == 999 && rb.value == -7 && rb.tag == 0x3C);
}

/* 测试项：删除表 */
static void t_delete(void)
{
    expect("delete: delete_table", fast_flash_delete_table(TBL_NAME) == 0);
    expect("delete: not_exists", fast_flash_table_exists(TBL_NAME) == false);
    /* 删除后重新创建，保证后续测试项可独立运行 */
    expect("delete: recreate",
           fast_flash_create_table(TBL_NAME, sizeof(sensor_rec_t), 32) == 0);
}

/* 测试项：垃圾回收 */
static void t_gc(void)
{
    /* 顺序写入大量数据制造碎片，再触发 GC，验证数据仍可读取 */
    sensor_rec_t base = { .seq = 0, .value = 0xABCDEF, .tag = 0x55 };
    for (uint32_t i = 0; i < 16; i++) {
        base.seq = (uint32_t)(1000 + i);
        if (fast_flash_write_table_data(TBL_NAME, &base, sizeof(base)) != 0) break;
    }
    expect("gc: before gc readable",
           fast_flash_read_table_data(TBL_NAME, 0, &(sensor_rec_t){0}, sizeof(sensor_rec_t)) == 0);
    expect("gc: run gc", fast_flash_gc() == 0);
    sensor_rec_t rb = {0};
    expect("gc: after gc readable",
           fast_flash_read_table_data(TBL_NAME, 0, &rb, sizeof(rb)) == 0);
    expect("gc: after gc match", rb.value == 0xABCDEF);
}

/* 测试项：掉电重放（重新初始化后数据仍在） */
static void t_powerloss(uint32_t total)
{
    /* 重建空表，避免与前面测试项的介质状态耦合 */
    if (fast_flash_table_exists(TBL_NAME)) {
        fast_flash_delete_table(TBL_NAME);
    }
    if (fast_flash_create_table(TBL_NAME, sizeof(sensor_rec_t), 32) != 0) {
        expect("powerloss: recreate table", false);
        return;
    }

    /* 预先写入一条固定记录（顺序写入） */
    sensor_rec_t rec = { .seq = 4242, .value = 777, .tag = 0x77 };
    expect("powerloss: write before reset",
           fast_flash_write_table_data(TBL_NAME, &rec, sizeof(rec)) == 0);

    /*
     * 模拟"掉电重启"：组件层重新初始化（重新加载管理表），
     * 但底层模拟基座（介质句柄与统计）保持复用，以保留"掉电前"
     * 的累计读写擦统计；介质持久性由已落盘的 BIN 文件保证。
     */
    expect("powerloss: reinit core", fast_flash_init(&sim_flash_ops, total, true) == 0);
    expect("powerloss: table persisted", fast_flash_table_exists(TBL_NAME) == true);

    sensor_rec_t rb = {0};
    expect("powerloss: read after reset",
           fast_flash_read_table_data(TBL_NAME, 0, &rb, sizeof(rb)) == 0);
    expect("powerloss: match", rb.seq == 4242 && rb.value == 777 && rb.tag == 0x77);
}

int main(void)
{
    printf("=== fast_flashdb_table 组件运行验证 ===\n");

    uint32_t total = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    if (fast_flash_sim_init_device(FLT_BIN) != 0) {
        printf("  [FAIL] 模拟基座初始化失败!\n");
        return 1;
    }
    if (fast_flash_init(&sim_flash_ops, total, true) != 0) {
        printf("  [FAIL] fast_flash_init 失败!\n");
        fast_flash_sim_deinit_device();
        return 1;
    }
    printf("  [OK  ] 组件初始化成功\n");

    /* 全新介质：先创建表，保证后续测试项可用 */
    if (!fast_flash_table_exists(TBL_NAME)) {
        if (fast_flash_create_table(TBL_NAME, sizeof(sensor_rec_t), 32) != 0) {
            printf("  [WARN] 默认表创建失败（可能残留），继续测试\n");
        }
    }

    const char *tests = getenv("FLT_TESTS");
    if (!tests || !*tests) tests = getenv("KV_TESTS");  /* 兼容后端统一注入键 */
    printf("\n[测试项] 启用的测试: %s\n", tests && *tests ? tests : "(全部)");

    if (has_test(tests, "init"))        { printf("\n[测试项] 初始化/建表\n");       t_init(); }
    if (has_test(tests, "write_read"))  { printf("\n[测试项] 基础写入/读取\n");     t_write_read(); }
    if (has_test(tests, "append"))      { printf("\n[测试项] 追加/计数\n");          t_append(); }
    if (has_test(tests, "update"))      { printf("\n[测试项] 按索引覆盖\n");        t_update(); }
    if (has_test(tests, "delete"))      { printf("\n[测试项] 删除表\n");            t_delete(); }
    if (has_test(tests, "gc"))          { printf("\n[测试项] 垃圾回收\n");          t_gc(); }
    if (has_test(tests, "powerloss"))   { printf("\n[测试项] 掉电重放\n");          t_powerloss(total); }

    /* 输出结构化统计（后端 parse_stats 解析） */
    {
        flash_stats_t fst;
        flash_sim_get_stats(fast_flash_sim_device(), &fst);
        printf("STATS_JSON:{\"reads\":%u,\"writes\":%u,\"erases\":%u,"
               "\"read_us\":%llu,\"write_us\":%llu,\"erase_us\":%llu,"
               "\"total_write_bytes\":%u,\"erase_cycles\":%u}\n",
               fst.total_reads, fst.total_writes, fst.total_erases,
               (unsigned long long)fst.read_time_us,
               (unsigned long long)fst.write_time_us,
               (unsigned long long)fst.erase_time_us,
               fst.total_write_bytes, 100000u);

        uint32_t nblk = flash_sim_block_count(fast_flash_sim_device());
        uint32_t *wm = NULL;
        if (nblk > 0) {
            wm = (uint32_t *)malloc(sizeof(uint32_t) * nblk);
            if (wm) {
                uint32_t got = flash_sim_get_wear_map(fast_flash_sim_device(), wm, nblk);
                printf("WEARMAP:");
                for (uint32_t i = 0; i < got; i++) printf("%s%u", i ? "," : "", wm[i]);
                printf("\n");
                free(wm);
            }
        }
    }

    printf("\n=== 自检结束: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    fast_flash_sim_deinit_device();
    return g_fail == 0 ? 0 : 1;
}
