/**
 * main_fs.c - 自研文件系统框架运行验证
 *
 * 本测试程序把 fs_store（简易块式文件系统）通过模拟基座运行起来，
 * 覆盖：多文件创建、多文件读写、某文件频繁修改、追加、删除、查询、
 * 掉电重放等能力，并输出结构化统计供后端解析。
 *
 * 测试驱动（环境变量，与本平台其它框架一致）：
 *   SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_CYCLES/SIM_RD_US/SIM_WR_US/
 *   SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R   模拟基座介质配置（默认 NOR）
 *   FS_TESTS    启用的测试项（逗号分隔），不设置则全部运行：
 *                 create      创建多个文件
 *                 write_read  多文件写入/读取
 *                 update      单文件频繁修改
 *                 append      追加写
 *                 delete      删除文件
 *                 query       大小/存在性查询
 *                 powerloss   掉电重放（重新初始化后数据仍在）
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...；级别前缀与
 * parse_output / _classify_line 约定一致：[INFO] [OK] [FAIL] [WARN]。
 */

#include "fs_store.h"
#include "flash_hal_adapter.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS_BIN "fs_demo.bin"

/* 测试配置：64KB 介质、4KB 块 -> FAT 1 块 + 15 个数据块 */
#define FS_TOTAL    (64u * 1024)
#define FS_BLOCK    4096u
#define FS_BASE     0u

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

/* 生成确定性内容（便于比对） */
static void fill_buf(uint8_t *buf, uint32_t len, uint32_t seed)
{
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)((seed + i * 7u + i / 13u) & 0xFF);
    }
}

/* 测试项：创建多个文件 */
static void t_create(const flash_hal_t *hal)
{
    expect("create: file_a",     fs_create(hal, "file_a") == FS_OK);
    expect("create: file_b", fs_create(hal, "file_b") == FS_OK);
    expect("create: file_c", fs_create(hal, "file_c") == FS_OK);
    expect("create: duplicate reject", fs_create(hal, "file_a") == FS_ERR_EXIST);
    expect("create: exists_a", fs_exists(hal, "file_a"));
    expect("create: count==3", fs_file_count(hal) == 3u);
}

/* 测试项：多文件写入/读取 */
static void t_write_read(const flash_hal_t *hal)
{
    uint8_t wb[512], rb[512];
    fill_buf(wb, sizeof(wb), 11);
    expect("write_read: write file_a(512B)",
           fs_write(hal, "file_a", wb, sizeof(wb)) == FS_OK);
    uint32_t len = sizeof(rb);
    expect("write_read: read file_a", fs_read(hal, "file_a", rb, 0, &len) == FS_OK);
    expect("write_read: len==512", len == sizeof(wb));
    expect("write_read: data match", memcmp(wb, rb, sizeof(wb)) == 0);

    uint8_t wb2[2048], rb2[2048];
    fill_buf(wb2, sizeof(wb2), 22);
    expect("write_read: write file_b(2KB)",
           fs_write(hal, "file_b", wb2, sizeof(wb2)) == FS_OK);
    len = sizeof(rb2);
    expect("write_read: read file_b", fs_read(hal, "file_b", rb2, 0, &len) == FS_OK);
    expect("write_read: data match_b", memcmp(wb2, rb2, sizeof(wb2)) == 0);
}

/* 测试项：某文件频繁修改（原地覆盖写复用 + 扩展/收缩） */
static void t_update(const flash_hal_t *hal)
{
    uint32_t rounds = (uint32_t)env_long("FS_ROUNDS", 30);
    uint8_t rb[256];
    for (uint32_t i = 0; i < rounds; i++) {
        uint8_t wb[256];
        fill_buf(wb, sizeof(wb), (uint32_t)(100 + i));
        if (fs_write(hal, "file_c", wb, sizeof(wb)) != FS_OK) {
            expect("update: write round", 0);
            return;
        }
    }
    uint8_t expect_buf[256];
    fill_buf(expect_buf, sizeof(expect_buf), (uint32_t)(100 + rounds - 1));
    uint32_t len = sizeof(rb);
    expect("update: read file_c", fs_read(hal, "file_c", rb, 0, &len) == FS_OK);
    expect("update: latest value", memcmp(rb, expect_buf, sizeof(rb)) == 0);

    /* 长度增长：触发扩展迁移 */
    uint8_t big[8192];
    fill_buf(big, sizeof(big), 77);
    expect("update: grow to 8KB", fs_write(hal, "file_c", big, sizeof(big)) == FS_OK);
    uint32_t rd = 256;
    expect("update: grown data",
           fs_read(hal, "file_c", rb, 0, &rd) == FS_OK && memcmp(rb, big, rd) == 0);

    /* 长度收缩：读回最新值 */
    uint8_t small[64];
    fill_buf(small, sizeof(small), 5);
    expect("update: shrink to 64B", fs_write(hal, "file_c", small, sizeof(small)) == FS_OK);
    len = sizeof(rb);
    expect("update: read shrunk", fs_read(hal, "file_c", rb, 0, &len) == FS_OK);
    expect("update: shrunk value", memcmp(rb, small, sizeof(small)) == 0);
}

/* 测试项：追加写（使用独立文件，验证追加内容与长度） */
static void t_append(const flash_hal_t *hal)
{
    expect("append: create", fs_create(hal, "log.txt") == FS_OK);
    const char *s1 = "hello ";
    const char *s2 = "world";
    expect("append: first",
           fs_append(hal, "log.txt", s1, (uint32_t)strlen(s1)) == FS_OK);
    expect("append: second",
           fs_append(hal, "log.txt", s2, (uint32_t)strlen(s2)) == FS_OK);
    char rb[64] = {0};
    uint32_t len = sizeof(rb);
    expect("append: read", fs_read(hal, "log.txt", rb, 0, &len) == FS_OK);
    expect("append: size==11", len == 11u);
    expect("append: concat", strncmp(rb, "hello world", 11) == 0);
    uint32_t size = 0;
    fs_get_size(hal, "log.txt", &size);
    expect("append: get_size==11", size == 11u);
}

/* 测试项：删除文件 */
static void t_delete(const flash_hal_t *hal)
{
    expect("delete: file_b", fs_delete(hal, "file_b") == FS_OK);
    expect("delete: not exists", !fs_exists(hal, "file_b"));
    expect("delete: missing reject", fs_delete(hal, "file_b") == FS_ERR_NOTFOUND);
    /* 删除后重新创建，验证空间复用 */
    expect("delete: recreate", fs_create(hal, "file_b") == FS_OK);
    uint8_t wb[100], rb[100];
    fill_buf(wb, sizeof(wb), 33);
    expect("delete: rewrite", fs_write(hal, "file_b", wb, sizeof(wb)) == FS_OK);
    uint32_t len = sizeof(rb);
    expect("delete: reread", fs_read(hal, "file_b", rb, 0, &len) == FS_OK);
    expect("delete: match", memcmp(wb, rb, sizeof(wb)) == 0);
}

/* 测试项：大小/存在性查询 */
static void t_query(const flash_hal_t *hal)
{
    uint32_t size = 0;
    expect("query: file_a size",
           fs_get_size(hal, "file_a", &size) == FS_OK && size > 0);
    expect("query: file_c exists", fs_exists(hal, "file_c"));
    expect("query: ghost missing", !fs_exists(hal, "ghost.txt"));
    uint32_t cnt = fs_file_count(hal);
    expect("query: count>0", cnt > 0);
    printf("  [info] 当前文件数=%u\n", cnt);
}

/* 测试项：掉电重放（重新初始化后数据仍在） */
#define POWERLOSS_BIN "fs_powerloss.bin"
static void t_powerloss(void)
{
    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = FLASH_TYPE_NOR;
    cfg.total_size = FS_TOTAL;
    cfg.erase_size = FS_BLOCK;
    cfg.write_size = 1;
    cfg.erase_cycles = 100000;
    cfg.bin_path = POWERLOSS_BIN;
    remove(POWERLOSS_BIN);
    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) { expect("powerloss: init", 0); return; }
    flash_hal_t hal;
    flash_hal_from_sim(dev, cfg.total_size, cfg.erase_size, cfg.write_size, &hal);
    fs_format(&hal, FS_BASE, FS_TOTAL, FS_BLOCK);
    uint8_t wb[512];
    fill_buf(wb, sizeof(wb), 0x55);
    expect("powerloss: write before reset",
           fs_write(&hal, "keep.txt", wb, sizeof(wb)) == FS_OK);
    /* 断电：释放设备（介质文件保留） */
    flash_sim_deinit(dev);

    /* 重新上电：重新初始化 fs 并读回数据 */
    flash_config_t cfg2;
    memset(&cfg2, 0, sizeof(cfg2));
    cfg2.type = FLASH_TYPE_NOR;
    cfg2.total_size = FS_TOTAL;
    cfg2.erase_size = FS_BLOCK;
    cfg2.write_size = 1;
    cfg2.erase_cycles = 100000;
    cfg2.bin_path = POWERLOSS_BIN;
    flash_dev_t *dev2 = flash_sim_init(&cfg2);
    if (!dev2) { expect("powerloss: re-init", 0); return; }
    flash_hal_t hal2;
    flash_hal_from_sim(dev2, cfg2.total_size, cfg2.erase_size, cfg2.write_size, &hal2);
    expect("powerloss: fs_load",
           fs_init(&hal2, FS_BASE, FS_TOTAL, FS_BLOCK) == FS_OK);
    expect("powerloss: file exists", fs_exists(&hal2, "keep.txt"));
    uint8_t rb[512] = {0};
    uint32_t len = sizeof(rb);
    expect("powerloss: read after reset",
           fs_read(&hal2, "keep.txt", rb, 0, &len) == FS_OK);
    expect("powerloss: data persisted", memcmp(wb, rb, sizeof(wb)) == 0);
    flash_sim_deinit(dev2);
}

int main(void)
{
    printf("=== 自研文件系统框架运行验证 ===\n");

    uint32_t total = (uint32_t)env_long("SIM_TOTAL", FS_TOTAL);
    uint32_t block = (uint32_t)env_long("SIM_ERASE", FS_BLOCK);
    remove(FS_BIN);   /* 保证每次从全新介质开始（可重复运行） */

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = total;
    cfg.erase_size = block;
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = FS_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);
    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) {
        printf("  [FAIL] Flash 初始化失败!\n");
        return 1;
    }
    flash_hal_t hal;
    flash_hal_from_sim(dev, cfg.total_size, cfg.erase_size, cfg.write_size, &hal);

    const char *tests = getenv("FS_TESTS");
    printf("\n[测试项] 启用的测试: %s\n", tests && *tests ? tests : "(全部)");

    fs_err_t rc = fs_init(&hal, FS_BASE, total, block);
    if (rc != FS_OK) {
        /* 全新介质：格式化 */
        if (fs_format(&hal, FS_BASE, total, block) != FS_OK) {
            printf("  [FAIL] FS 格式化失败!\n");
            flash_sim_deinit(dev);
            return 1;
        }
    }
    printf("  [OK  ] FS 初始化/格式化成功 (total=%u block=%u)\n", total, block);

    if (has_test(tests, "create"))     { printf("\n[测试项] 创建多个文件\n");     t_create(&hal); }
    if (has_test(tests, "write_read")) { printf("\n[测试项] 多文件写入/读取\n");   t_write_read(&hal); }
    if (has_test(tests, "update"))     { printf("\n[测试项] 单文件频繁修改\n");    t_update(&hal); }
    if (has_test(tests, "append"))     { printf("\n[测试项] 追加写\n");            t_append(&hal); }
    if (has_test(tests, "delete"))     { printf("\n[测试项] 删除文件\n");          t_delete(&hal); }
    if (has_test(tests, "query"))      { printf("\n[测试项] 大小/存在性查询\n");   t_query(&hal); }
    if (has_test(tests, "powerloss"))  { printf("\n[测试项] 掉电重放\n");          t_powerloss(); }

    /* 输出结构化统计（后端解析） */
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
               st.bad_block_count, cfg.erase_cycles);
        uint32_t nblk = flash_sim_block_count(dev);
        if (nblk > 0) {
            uint32_t *wm = (uint32_t *)malloc(sizeof(uint32_t) * nblk);
            if (wm) {
                uint32_t got = flash_sim_get_wear_map(dev, wm, nblk);
                printf("WEARMAP:");
                for (uint32_t i = 0; i < got; i++) {
                    printf("%s%u", i ? "," : "", wm[i]);
                }
                printf("\n");
                free(wm);
            }
        }
    }

    printf("\n=== 文件系统验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    flash_sim_deinit(dev);
    return g_fail == 0 ? 0 : 1;
}
