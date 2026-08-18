/**
 * main_yaffs.c - YAFFS 开源文件系统组件运行验证
 *
 * 本测试程序把 YAFFS（yaffs Direct 用户空间接口）通过移植层
 * （yaffs_sim_port.c）对接模拟基座运行起来，覆盖：挂载、多文件创建、
 * 多文件读写、某文件频繁修改、追加、删除、查询与掉电重放等能力，
 * 并输出结构化统计供后端解析。
 *
 * 测试驱动（环境变量，与本平台其它框架一致）：
 *   SIM_TYPE/SIM_TOTAL/SIM_ERASE/SIM_CYCLES/SIM_RD_US/SIM_WR_US/
 *   SIM_ERASE_US/SIM_BAD_N/SIM_BAD_R   模拟基座介质配置（默认 NOR）
 *   YAFFS_TESTS 启用的测试项（逗号分隔），不设置则全部运行：
 *                 mount       挂载
 *                 create      创建多个文件
 *                 write_read  多文件写入/读取
 *                 update      单文件频繁修改
 *                 append      追加写
 *                 delete      删除文件
 *                 query       大小/存在性查询
 *                 powerloss   掉电重放（卸载后重新挂载，数据仍在）
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...；级别前缀与
 * parse_output / _classify_line 约定一致：[INFO] [OK] [FAIL] [WARN]。
 */

#include "yaffs_sim_port.h"
#include "flash_sim.h"

#include "yaffsfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YAFFS_BIN "yaffs_sim.bin"

/* 设备根路径 */
#define YAFFS_ROOT "/flash"

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

/* 写文件内容（不存在则创建/截断） */
static int file_write_all(const char *path, const void *buf, uint32_t len)
{
    int fd = yaffs_open(path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) { return -1; }
    int w = yaffs_write(fd, buf, len);
    yaffs_close(fd);
    return (w == (int)len) ? 0 : -1;
}

/* 测试项：挂载 */
static void t_mount(void)
{
    int rc = yaffs_mount(YAFFS_ROOT);
    expect("mount: yaffs_mount", rc >= 0);
}

/* 测试项：创建多个文件（句柄用后即关，避免占用设备导致无法卸载） */
static void t_create(void)
{
    int fd = yaffs_open(YAFFS_ROOT "/file_a", O_CREAT | O_RDWR, 0666);
    expect("create: file_a", fd >= 0);
    if (fd >= 0) { yaffs_close(fd); }
    fd = yaffs_open(YAFFS_ROOT "/file_b", O_CREAT | O_RDWR, 0666);
    expect("create: file_b", fd >= 0);
    if (fd >= 0) { yaffs_close(fd); }
    fd = yaffs_open(YAFFS_ROOT "/file_c", O_CREAT | O_RDWR, 0666);
    expect("create: file_c", fd >= 0);
    if (fd >= 0) { yaffs_close(fd); }
}

/* 测试项：多文件写入/读取 */
static void t_write_read(void)
{
    uint8_t wb[512], rb[512];
    fill_buf(wb, sizeof(wb), 11);
    expect("write_read: write file_a(512B)",
           file_write_all(YAFFS_ROOT "/file_a", wb, sizeof(wb)) == 0);
    int fd = yaffs_open(YAFFS_ROOT "/file_a", O_RDONLY, 0);
    expect("write_read: open", fd >= 0);
    int rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("write_read: read 512B", rd == (int)sizeof(wb));
    expect("write_read: data match", memcmp(wb, rb, sizeof(wb)) == 0);

    uint8_t wb2[2048], rb2[2048];
    fill_buf(wb2, sizeof(wb2), 22);
    expect("write_read: write file_b(2KB)",
           file_write_all(YAFFS_ROOT "/file_b", wb2, sizeof(wb2)) == 0);
    fd = yaffs_open(YAFFS_ROOT "/file_b", O_RDONLY, 0);
    expect("write_read: open_b", fd >= 0);
    rd = yaffs_read(fd, rb2, sizeof(rb2));
    yaffs_close(fd);
    expect("write_read: data match_b", rd == (int)sizeof(wb2)
           && memcmp(wb2, rb2, sizeof(wb2)) == 0);
}

/* 测试项：某文件频繁修改（同长覆盖 + 增长/收缩） */
static void t_update(void)
{
    uint8_t rb[256];
    for (int i = 0; i < 30; i++) {
        uint8_t wb[256];
        fill_buf(wb, sizeof(wb), (uint32_t)(100 + i));
        if (file_write_all(YAFFS_ROOT "/file_c", wb, sizeof(wb)) != 0) {
            expect("update: write round", 0);
            return;
        }
    }
    uint8_t expect_buf[256];
    fill_buf(expect_buf, sizeof(expect_buf), 129);
    int fd = yaffs_open(YAFFS_ROOT "/file_c", O_RDONLY, 0);
    expect("update: open", fd >= 0);
    int rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("update: latest value", rd == (int)sizeof(rb)
           && memcmp(rb, expect_buf, sizeof(rb)) == 0);

    /* 增长到 8KB */
    uint8_t big[8192];
    fill_buf(big, sizeof(big), 77);
    expect("update: grow to 8KB",
           file_write_all(YAFFS_ROOT "/file_c", big, sizeof(big)) == 0);
    fd = yaffs_open(YAFFS_ROOT "/file_c", O_RDONLY, 0);
    expect("update: open grown", fd >= 0);
    rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("update: grown data", rd == (int)sizeof(rb)
           && memcmp(rb, big, sizeof(rb)) == 0);

    /* 收缩到 64B */
    uint8_t small[64];
    fill_buf(small, sizeof(small), 5);
    expect("update: shrink to 64B",
           file_write_all(YAFFS_ROOT "/file_c", small, sizeof(small)) == 0);
    fd = yaffs_open(YAFFS_ROOT "/file_c", O_RDONLY, 0);
    expect("update: open shrunk", fd >= 0);
    rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("update: shrunk value", rd == (int)sizeof(small)
           && memcmp(rb, small, sizeof(small)) == 0);
}

/* 测试项：追加写 */
static void t_append(void)
{
    int fd = yaffs_open(YAFFS_ROOT "/log.txt", O_CREAT | O_RDWR, 0666);
    expect("append: open create", fd >= 0);
    const char *s1 = "hello ";
    const char *s2 = "world";
    expect("append: first",
           yaffs_write(fd, s1, (unsigned int)strlen(s1)) == (int)strlen(s1));
    expect("append: second",
           yaffs_write(fd, s2, (unsigned int)strlen(s2)) == (int)strlen(s2));
    struct yaffs_stat st;
    expect("append: size",
           yaffs_fstat(fd, &st) >= 0 && st.st_size == 11);
    yaffs_close(fd);

    char rb[64] = {0};
    fd = yaffs_open(YAFFS_ROOT "/log.txt", O_RDONLY, 0);
    expect("append: open read", fd >= 0);
    int rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("append: concat", rd == 11 && strncmp(rb, "hello world", 11) == 0);
}

/* 测试项：删除文件 */
static void t_delete(void)
{
    expect("delete: file_b", yaffs_unlink(YAFFS_ROOT "/file_b") >= 0);
    expect("delete: not exists",
           yaffs_open(YAFFS_ROOT "/file_b", O_RDONLY, 0) < 0);
    /* 删除后重新创建，验证空间复用 */
    uint8_t wb[100], rb[100];
    fill_buf(wb, sizeof(wb), 33);
    expect("delete: recreate",
           file_write_all(YAFFS_ROOT "/file_b", wb, sizeof(wb)) == 0);
    int fd = yaffs_open(YAFFS_ROOT "/file_b", O_RDONLY, 0);
    expect("delete: open", fd >= 0);
    int rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("delete: match", rd == (int)sizeof(wb)
           && memcmp(wb, rb, sizeof(wb)) == 0);
}

/* 测试项：大小/存在性查询 */
static void t_query(void)
{
    struct yaffs_stat st;
    expect("query: file_a stat",
           yaffs_stat(YAFFS_ROOT "/file_a", &st) >= 0);
    if (yaffs_stat(YAFFS_ROOT "/file_a", &st) >= 0) {
        expect("query: file_a size>0", st.st_size > 0);
    }
    expect("query: missing",
           yaffs_stat(YAFFS_ROOT "/ghost.txt", &st) < 0);
}

/* 测试项：掉电重放（卸载后重新挂载，数据仍在） */
static void t_powerloss(void)
{
    uint8_t wb[512];
    fill_buf(wb, sizeof(wb), 0x55);
    expect("powerloss: write keep.txt",
           file_write_all(YAFFS_ROOT "/keep.txt", wb, sizeof(wb)) == 0);
    /* 断电：卸载（介质文件保留） */
    expect("powerloss: unmount", yaffs_unmount(YAFFS_ROOT) >= 0);
    /* 重新上电：重新挂载并验证 keep.txt 仍在 */
    expect("powerloss: remount", yaffs_mount(YAFFS_ROOT) >= 0);
    int fd = yaffs_open(YAFFS_ROOT "/keep.txt", O_RDONLY, 0);
    expect("powerloss: open keep", fd >= 0);
    uint8_t rb[512] = {0};
    int rd = yaffs_read(fd, rb, sizeof(rb));
    yaffs_close(fd);
    expect("powerloss: data persisted", rd == (int)sizeof(wb)
           && memcmp(wb, rb, sizeof(wb)) == 0);
}

int main(void)
{
    printf("=== YAFFS 组件运行验证 ===\n");

    uint32_t total = (uint32_t)env_long("SIM_TOTAL", YAFFS_SIM_BLOCKS
                                        * YAFFS_SIM_CHUNKS_PER_BLOCK
                                        * (YAFFS_SIM_CHUNK_BYTES
                                           + YAFFS_SIM_SPARE_BYTES));
    remove(YAFFS_BIN);   /* 保证每次从全新介质开始（可重复运行） */

    if (yaffs_sim_init_device(YAFFS_BIN) != 0) {
        printf("  [FAIL] 模拟基座初始化失败!\n");
        return 1;
    }
    if (yaffs_sim_start_up() != 0) {
        printf("  [FAIL] YAFFS 设备注册失败!\n");
        yaffs_sim_deinit_device();
        return 1;
    }
    printf("  [OK  ] 模拟基座初始化成功 (total=%u)\n", total);

    const char *tests = getenv("YAFFS_TESTS");
    printf("\n[测试项] 启用的测试: %s\n", tests && *tests ? tests : "(全部)");

    if (has_test(tests, "mount"))      { printf("\n[测试项] 挂载\n");            t_mount(); }
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
        flash_sim_get_stats(yaffs_sim_device(), &st);
        printf("STATS_JSON:{\"reads\":%u,\"writes\":%u,\"erases\":%u,"
               "\"write_bytes\":%u,\"max_cycles\":%u,\"avg_cycles\":%u,"
               "\"read_us\":%llu,\"write_us\":%llu,\"erase_us\":%llu,"
               "\"bad_blocks\":%u,\"erase_cycles\":%u}\n",
               st.total_reads, st.total_writes, st.total_erases,
               st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
               (unsigned long long)st.read_time_us,
               (unsigned long long)st.write_time_us,
               (unsigned long long)st.erase_time_us,
               st.bad_block_count, 100000u);
        uint32_t nblk = flash_sim_block_count(yaffs_sim_device());
        if (nblk > 0) {
            uint32_t *wm = (uint32_t *)malloc(sizeof(uint32_t) * nblk);
            if (wm) {
                uint32_t got = flash_sim_get_wear_map(yaffs_sim_device(), wm, nblk);
                printf("WEARMAP:");
                for (uint32_t i = 0; i < got; i++) {
                    printf("%s%u", i ? "," : "", wm[i]);
                }
                printf("\n");
                free(wm);
            }
        }
    }

    yaffs_sim_deinit_device();
    printf("\n=== YAFFS 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
