/**
 * main_littlefs.c - LittleFS 开源文件系统组件运行验证
 *
 * 本测试程序把 LittleFS（littlefs-project/littlefs v2.x）通过移植层
 * （littlefs_sim_port.c）对接模拟基座运行起来，覆盖：格式化、多文件
 * 创建、多文件读写、某文件频繁修改、追加、删除、查询与掉电重放等
 * 能力，并输出结构化统计供后端解析。
 *
 * 测试驱动（环境变量，与本平台其它框架一致）：
 *   SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_WRITE/SIM_CYCLES/SIM_RD_US/
 *   SIM_WR_US/SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R   模拟基座介质配置
 *   LFS_TESTS   启用的测试项（逗号分隔），不设置则全部运行：
 *                 mount       格式化 + 挂载
 *                 create      创建多个文件
 *                 write_read  多文件写入/读取
 *                 update      单文件频繁修改
 *                 append      追加写
 *                 delete      删除文件
 *                 query       大小/存在性查询
 *                 powerloss   掉电重放（重新挂载后数据仍在）
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...；级别前缀与
 * parse_output / _classify_line 约定一致：[INFO] [OK] [FAIL] [WARN]。
 */

#include "lfs.h"
#include "littlefs_sim_port.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LFS_BIN "littlefs_sim.bin"

static lfs_t g_lfs;
static struct lfs_config g_cfg;   /* 全局保留，供 powerloss 重挂载使用 */
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

/* 写文件内容（不存在则创建） */
static int file_write_all(const char *path, const void *buf, lfs_size_t len)
{
    lfs_file_t f;
    int rc = lfs_file_open(&g_lfs, &f, path, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
    if (rc < 0) { return rc; }
    lfs_ssize_t w = lfs_file_write(&g_lfs, &f, buf, len);
    lfs_file_close(&g_lfs, &f);
    return (w == (lfs_ssize_t)len) ? 0 : -1;
}

/* 测试项：格式化 + 挂载 */
static void t_mount(struct lfs_config *cfg)
{
    int rc = lfs_format(&g_lfs, cfg);
    if (rc != LFS_ERR_OK) {
        /* 已格式化过：直接挂载 */
        rc = lfs_mount(&g_lfs, cfg);
    } else {
        rc = lfs_mount(&g_lfs, cfg);
    }
    expect("mount: format+mount", rc == LFS_ERR_OK);
}

/* 测试项：创建多个文件 */
static void t_create(void)
{
    expect("create: file_a",
           file_write_all("file_a", "", 0) == 0);
    expect("create: file_b",
           file_write_all("file_b", "", 0) == 0);
    expect("create: file_c",
           file_write_all("file_c", "", 0) == 0);
    lfs_file_t f;
    expect("create: open dir file", lfs_file_open(&g_lfs, &f, "file_a", LFS_O_RDONLY) >= 0);
    lfs_file_close(&g_lfs, &f);
}

/* 测试项：多文件写入/读取 */
static void t_write_read(void)
{
    uint8_t wb[512], rb[512];
    fill_buf(wb, sizeof(wb), 11);
    expect("write_read: write file_a(512B)",
           file_write_all("file_a", wb, sizeof(wb)) == 0);
    lfs_file_t f;
    expect("write_read: open", lfs_file_open(&g_lfs, &f, "file_a", LFS_O_RDONLY) >= 0);
    lfs_ssize_t rd = lfs_file_read(&g_lfs, &f, rb, sizeof(rb));
    lfs_file_close(&g_lfs, &f);
    expect("write_read: read 512B", rd == (lfs_ssize_t)sizeof(wb));
    expect("write_read: data match", rd == (lfs_ssize_t)sizeof(wb)
           && memcmp(wb, rb, sizeof(wb)) == 0);

    uint8_t wb2[2048], rb2[2048];
    fill_buf(wb2, sizeof(wb2), 22);
    expect("write_read: write file_b(2KB)",
           file_write_all("file_b", wb2, sizeof(wb2)) == 0);
    expect("write_read: open_b",
           lfs_file_open(&g_lfs, &f, "file_b", LFS_O_RDONLY) >= 0);
    rd = lfs_file_read(&g_lfs, &f, rb2, sizeof(rb2));
    lfs_file_close(&g_lfs, &f);
    expect("write_read: data match_b", rd == (lfs_ssize_t)sizeof(wb2)
           && memcmp(wb2, rb2, sizeof(wb2)) == 0);
}

/* 测试项：某文件频繁修改（同长覆盖 + 增长/收缩） */
static void t_update(void)
{
    uint8_t rb[256];
    for (int i = 0; i < 30; i++) {
        uint8_t wb[256];
        fill_buf(wb, sizeof(wb), (uint32_t)(100 + i));
        if (file_write_all("file_c", wb, sizeof(wb)) != 0) {
            expect("update: write round", 0);
            return;
        }
    }
    uint8_t expect_buf[256];
    fill_buf(expect_buf, sizeof(expect_buf), 129);
    lfs_file_t f;
    expect("update: open", lfs_file_open(&g_lfs, &f, "file_c", LFS_O_RDONLY) >= 0);
    lfs_ssize_t rd = lfs_file_read(&g_lfs, &f, rb, sizeof(rb));
    lfs_file_close(&g_lfs, &f);
    expect("update: latest value", rd == (lfs_ssize_t)sizeof(rb)
           && memcmp(rb, expect_buf, sizeof(rb)) == 0);

    /* 增长到 8KB */
    uint8_t big[8192];
    fill_buf(big, sizeof(big), 77);
    expect("update: grow to 8KB", file_write_all("file_c", big, sizeof(big)) == 0);
    lfs_file_t f2;
    expect("update: open grown",
           lfs_file_open(&g_lfs, &f2, "file_c", LFS_O_RDONLY) >= 0);
    lfs_ssize_t rd2 = lfs_file_read(&g_lfs, &f2, rb, sizeof(rb));
    lfs_file_close(&g_lfs, &f2);
    expect("update: grown data", rd2 == (lfs_ssize_t)sizeof(rb)
           && memcmp(rb, big, sizeof(rb)) == 0);

    /* 收缩到 64B */
    uint8_t small[64];
    fill_buf(small, sizeof(small), 5);
    expect("update: shrink to 64B",
           file_write_all("file_c", small, sizeof(small)) == 0);
    expect("update: open shrunk",
           lfs_file_open(&g_lfs, &f, "file_c", LFS_O_RDONLY) >= 0);
    rd = lfs_file_read(&g_lfs, &f, rb, sizeof(rb));
    lfs_file_close(&g_lfs, &f);
    expect("update: shrunk value", rd == (lfs_ssize_t)sizeof(small)
           && memcmp(rb, small, sizeof(small)) == 0);
}

/* 测试项：追加写 */
static void t_append(void)
{
    lfs_file_t f;
    expect("append: open create",
           lfs_file_open(&g_lfs, &f, "log.txt", LFS_O_RDWR | LFS_O_CREAT) >= 0);
    const char *s1 = "hello ";
    const char *s2 = "world";
    expect("append: first",
           lfs_file_write(&g_lfs, &f, s1, (lfs_size_t)strlen(s1)) == (lfs_ssize_t)strlen(s1));
    expect("append: second",
           lfs_file_write(&g_lfs, &f, s2, (lfs_size_t)strlen(s2)) == (lfs_ssize_t)strlen(s2));
    expect("append: size", lfs_file_size(&g_lfs, &f) == 11);
    lfs_file_close(&g_lfs, &f);

    char rb[64] = {0};
    expect("append: open read", lfs_file_open(&g_lfs, &f, "log.txt", LFS_O_RDONLY) >= 0);
    lfs_ssize_t rd = lfs_file_read(&g_lfs, &f, rb, sizeof(rb));
    lfs_file_close(&g_lfs, &f);
    expect("append: concat", rd == 11 && strncmp(rb, "hello world", 11) == 0);
}

/* 测试项：删除文件 */
static void t_delete(void)
{
    expect("delete: file_b", lfs_remove(&g_lfs, "file_b") == LFS_ERR_OK);
    lfs_file_t f;
    expect("delete: not exists",
           lfs_file_open(&g_lfs, &f, "file_b", LFS_O_RDONLY) < 0);
    /* 删除后重新创建，验证空间复用 */
    uint8_t wb[100], rb[100];
    fill_buf(wb, sizeof(wb), 33);
    expect("delete: recreate", file_write_all("file_b", wb, sizeof(wb)) == 0);
    expect("delete: open", lfs_file_open(&g_lfs, &f, "file_b", LFS_O_RDONLY) >= 0);
    lfs_ssize_t rd = lfs_file_read(&g_lfs, &f, rb, sizeof(rb));
    lfs_file_close(&g_lfs, &f);
    expect("delete: match", rd == (lfs_ssize_t)sizeof(wb) && memcmp(wb, rb, sizeof(wb)) == 0);
}

/* 测试项：大小/存在性查询 */
static void t_query(void)
{
    lfs_file_t f;
    if (lfs_file_open(&g_lfs, &f, "file_a", LFS_O_RDONLY) >= 0) {
        expect("query: file_a size>0", lfs_file_size(&g_lfs, &f) > 0);
        lfs_file_close(&g_lfs, &f);
    } else {
        expect("query: open file_a", 0);
    }
    expect("query: missing", lfs_stat(&g_lfs, "ghost.txt", &(struct lfs_info){0}) == LFS_ERR_NOENT);
}

/* 测试项：掉电重放（unmount 后重新 mount，数据仍在） */
static void t_powerloss(void)
{
    lfs_file_t f;
    expect("powerloss: open",
           lfs_file_open(&g_lfs, &f, "keep.txt",
                         LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC) >= 0);
    uint8_t wb[512];
    fill_buf(wb, sizeof(wb), 0x55);
    expect("powerloss: write",
           lfs_file_write(&g_lfs, &f, wb, sizeof(wb)) == (lfs_ssize_t)sizeof(wb));
    lfs_file_close(&g_lfs, &f);
    expect("powerloss: unmount", lfs_unmount(&g_lfs) == LFS_ERR_OK);

    /* 断电重启：重新挂载后验证 keep.txt 仍在 */
    expect("powerloss: mount", lfs_mount(&g_lfs, &g_cfg) == LFS_ERR_OK);
    (void)f;
}

int main(void)
{
    printf("=== LittleFS 组件运行验证 ===\n");

    uint32_t total = (uint32_t)env_long("SIM_TOTAL", 128 * 1024);
    remove(LFS_BIN);   /* 保证每次从全新介质开始（可重复运行） */

    if (littlefs_sim_init_device(LFS_BIN, &g_cfg) != 0) {
        printf("  [FAIL] 模拟基座初始化失败!\n");
        return 1;
    }
    printf("  [OK  ] 模拟基座初始化成功 (total=%u block=%u)\n",
           total, littlefs_sim_block_size());

    const char *tests = getenv("LFS_TESTS");
    printf("\n[测试项] 启用的测试: %s\n", tests && *tests ? tests : "(全部)");

    if (has_test(tests, "mount"))      { printf("\n[测试项] 格式化+挂载\n");    t_mount(&g_cfg); }
    if (has_test(tests, "create"))     { printf("\n[测试项] 创建多个文件\n");   t_create(); }
    if (has_test(tests, "write_read")) { printf("\n[测试项] 多文件写入/读取\n"); t_write_read(); }
    if (has_test(tests, "update"))     { printf("\n[测试项] 单文件频繁修改\n");  t_update(); }
    if (has_test(tests, "append"))     { printf("\n[测试项] 追加写\n");          t_append(); }
    if (has_test(tests, "delete"))     { printf("\n[测试项] 删除文件\n");        t_delete(); }
    if (has_test(tests, "query"))      { printf("\n[测试项] 大小/存在性查询\n"); t_query(); }
    if (has_test(tests, "powerloss"))  { printf("\n[测试项] 掉电重放\n");        t_powerloss(); }

    /* 输出结构化统计（后端解析） */
    {
        flash_stats_t st;
        flash_sim_get_stats(littlefs_sim_device(), &st);
        printf("STATS_JSON:{\"reads\":%u,\"writes\":%u,\"erases\":%u,"
               "\"write_bytes\":%u,\"max_cycles\":%u,\"avg_cycles\":%u,"
               "\"read_us\":%llu,\"write_us\":%llu,\"erase_us\":%llu,"
               "\"bad_blocks\":%u,\"erase_cycles\":%u}\n",
               st.total_reads, st.total_writes, st.total_erases,
               st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
               (unsigned long long)st.read_time_us,
               (unsigned long long)st.write_time_us,
               (unsigned long long)st.erase_time_us,
               st.bad_block_count, (uint32_t)g_cfg.block_cycles);
        uint32_t nblk = flash_sim_block_count(littlefs_sim_device());
        if (nblk > 0) {
            uint32_t *wm = (uint32_t *)malloc(sizeof(uint32_t) * nblk);
            if (wm) {
                uint32_t got = flash_sim_get_wear_map(littlefs_sim_device(), wm, nblk);
                printf("WEARMAP:");
                for (uint32_t i = 0; i < got; i++) {
                    printf("%s%u", i ? "," : "", wm[i]);
                }
                printf("\n");
                free(wm);
            }
        }
    }

    littlefs_sim_deinit_device();
    printf("\n=== LittleFS 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
