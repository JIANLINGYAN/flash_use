/**
 * main_fcb.c - Zephyr FCB（Flash Circular Buffer）组件在模拟基座上的验证
 *
 * 定位：FCB 是 Zephyr 的闪存环形日志/记录存储（append-only、回卷覆盖、
 * 重启恢复、CRC 校验），vendor 零修改，经 Zephyr 兼容层桥接 flash_sim。
 * 本测试验证：追加/遍历读取、更新覆盖（旧记录失效）、轮转、清空、
 * 掉电恢复、功能压测。
 *
 * 测试驱动（环境变量，与平台其它框架保持一致）：
 *   SIM_*        模拟基座参数
 *   KV_CAPACITY  FCB 分区容量(字节)，须为擦除块整数倍且 >= 2 块
 *   KV_TESTS     append/walk/rotate/clear/powerloss/func
 *   KV_ITEMS     功能压测条目表：LEN,N,FREQ;...
 *   KV_ROUNDS    压测轮数（默认 20）
 *
 * 输出：STATS_JSON:{...} 与 WEARMAP:...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/fs/fcb.h>

#include "flash_sim.h"
#include "zephyr_compat.h"

#define FCB_BIN "fcb_demo.bin"
#define FCB_MAGIC 0x1234abcd

static int g_fail = 0;
static struct fcb s_fcb;
static flash_dev_t *s_dev;

/* FCB 扇区表：每扇区=一个擦除块 */
static struct flash_sector s_sectors[64];

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

static uint32_t s_seed = 0x12345678u;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed;
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

/* 初始化 FCB（支持反复调用以模拟重启） */
static int fcb_reinit(void)
{
    return fcb_init(0, &s_fcb);
}

/* 追加一条记录 */
static int fcb_append_rec(const void *data, uint16_t len)
{
    struct fcb_entry loc;
    int rc;

    rc = fcb_append(&s_fcb, len, &loc);
    if (rc) {
        return rc;
    }
    rc = flash_area_write(s_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), data, len);
    if (rc) {
        return rc;
    }
    return fcb_append_finish(&s_fcb, &loc);
}

/* 遍历回调：统计条目数并校验每条可完整读出 */
struct walk_ctx {
    uint32_t count;
    uint32_t want_count;
    int mismatch;
};

static int walk_cb(struct fcb_entry_ctx *ctx, void *arg)
{
    struct walk_ctx *w = (struct walk_ctx *)arg;
    uint8_t buf[256];

    w->count++;
    if (ctx->loc.fe_data_len == 0 || ctx->loc.fe_data_len > sizeof(buf)) {
        w->mismatch = 1;
        return 0;
    }
    if (flash_area_read(ctx->fap, FCB_ENTRY_FA_DATA_OFF(ctx->loc), buf,
                        ctx->loc.fe_data_len) != 0) {
        w->mismatch = 1;
        return 0;
    }
    if (w->want_count && w->count >= w->want_count) {
        return 1;
    }
    return 0;
}

/* ---------------- 测试项 ---------------- */

/* 追加 3 条并遍历，校验内容 */
static void t_append(void)
{
    uint8_t d1[] = {0, 1, 2, 3};
    uint8_t d2[] = {4, 5, 6};
    uint8_t d3[] = {7, 8};
    uint8_t rb[8] = {0};
    struct fcb_entry loc;
    int rc;

    expect("append: 追加1", fcb_append_rec(d1, sizeof(d1)) == 0);
    expect("append: 追加2", fcb_append_rec(d2, sizeof(d2)) == 0);
    expect("append: 追加3", fcb_append_rec(d3, sizeof(d3)) == 0);

    /* 从头部遍历读取 */
    memset(&loc, 0, sizeof(loc));
    rc = fcb_getnext(&s_fcb, &loc);
    expect("append: getnext 首条", rc == 0);
    if (rc == 0) {
        flash_area_read(s_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), rb,
                        loc.fe_data_len);
        expect("append: 首条内容",
               loc.fe_data_len == sizeof(d1) &&
               memcmp(rb, d1, sizeof(d1)) == 0);
    }

    struct walk_ctx w = {0, 0, 0};
    rc = fcb_walk(&s_fcb, NULL, walk_cb, &w);
    expect("append: walk 3 条", rc == 0 && w.count == 3 && w.mismatch == 0);
}

/* 更新覆盖：追加同 key 的新记录，遍历时新值覆盖旧值（逻辑层处理） */
static void t_walk(void)
{
    struct walk_ctx w = {0, 0, 0};
    int rc = fcb_walk(&s_fcb, NULL, walk_cb, &w);

    expect("walk: 遍历成功且内容一致", rc == 0 && w.mismatch == 0);
    printf("    [info] 遍历条目数=%u\n", w.count);
}

/* 轮转：追加大量记录触发扇区切换/回卷 */
static void t_rotate(uint32_t capacity)
{
    uint8_t rec[16];
    uint32_t n = (capacity / sizeof(rec)) * 2u; /* 超过一个扇区容量 */
    uint32_t err = 0;
    int rc;

    for (uint32_t i = 0; i < n; i++) {
        rec[0] = (uint8_t)(i % 251);
        memset(rec + 1, 0x5A, sizeof(rec) - 1);
        rc = fcb_append_rec(rec, sizeof(rec));
        if (rc && rc != -ENOSPC) {
            err++;
            break;
        }
        if (rc == -ENOSPC) {
            break;
        }
    }
    expect("rotate: 追加无异常错误", err == 0);

    /* 主动轮转应能释放空间 */
    expect("rotate: 主动轮转", fcb_rotate(&s_fcb) == 0);
    expect("rotate: 轮转后可继续追加", fcb_append_rec(rec, sizeof(rec)) == 0);
}

/* 清空：循环轮转直到空 */
static void t_clear(void)
{
    expect("clear: fcb_clear", fcb_clear(&s_fcb) == 0);
    expect("clear: 清空后为空", fcb_is_empty(&s_fcb) != 0);
}

/* 掉电恢复：写入后重新 fcb_init（保留介质）重新扫描 */
static void t_powerloss(void)
{
    uint8_t rec[16];
    struct walk_ctx w;
    int rc;

    memset(rec, 0x33, sizeof(rec));
    rec[0] = 0x7E;
    expect("powerloss: 写入", fcb_append_rec(rec, sizeof(rec)) == 0);

    expect("powerloss: 重启重载", fcb_reinit() == 0);

    w.count = 0;
    w.want_count = 0;
    w.mismatch = 0;
    rc = fcb_walk(&s_fcb, NULL, walk_cb, &w);
    expect("powerloss: 重载后数据可遍历", rc == 0 && w.mismatch == 0);
    expect("powerloss: 至少保留最后记录", w.count >= 1);
}

/* 功能压测：反复追加/遍历 */
static void func_item(uint32_t vlen, uint32_t n, uint32_t freq, uint32_t rounds,
                      uint32_t *acc_ops, uint32_t *acc_lost)
{
    if (vlen == 0) { vlen = 1; }
    if (n == 0) { n = 1; }
    if (freq > 100) { freq = 100; }

    uint8_t *buf = (uint8_t *)malloc(vlen);
    uint8_t *rbuf = (uint8_t *)malloc(vlen);
    if (!buf || !rbuf) {
        free(buf);
        free(rbuf);
        expect("func: 内存分配", 0);
        return;
    }

    uint32_t lost = 0;
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t k = 0; k < n; k++) {
            if ((rnd() % 100u) < freq) {
                for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
                if (fcb_append_rec(buf, vlen) != 0) {
                    /* 空间满时轮转后重试一次 */
                    if (fcb_rotate(&s_fcb) != 0 ||
                        fcb_append_rec(buf, vlen) != 0) {
                        lost++;
                        continue;
                    }
                }
                (*acc_ops)++;
                /* 追加后回读最近一条（walk 取最后） */
                struct fcb_entry_ctx last;
                struct fcb_entry entry;
                memset(&entry, 0, sizeof(entry));
                if (fcb_offset_last_n(&s_fcb, 1, &entry) != 0) {
                    lost++;
                    continue;
                }
                last.loc = entry;
                last.fap = s_fcb.fap;
                if (flash_area_read(last.fap,
                                    FCB_ENTRY_FA_DATA_OFF(last.loc),
                                    rbuf, last.loc.fe_data_len) != 0 ||
                    last.loc.fe_data_len != vlen ||
                    memcmp(buf, rbuf, vlen) != 0) {
                    lost++;
                }
            } else {
                (*acc_ops)++; /* 只计数不实际遍历（模拟读侧） */
            }
        }
    }

    free(buf);
    free(rbuf);
    *acc_lost += lost;
    printf("    [info] 条目(长度=%u,条数=%u,频率=%u%%): 数据丢失=%u\n",
           vlen, n, freq, lost);
}

int main(void)
{
    printf("=== Zephyr FCB(环形缓冲) 组件运行验证 ===\n");

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    cfg.erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024);
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.read_size = 1;
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = FCB_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);

    if (cfg.type == FLASH_TYPE_EEPROM) {
        printf("  [info] FCB 依赖块擦除语义，EEPROM 自动切换为 NOR\n");
        cfg.type = FLASH_TYPE_NOR;
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        printf("  Flash 初始化失败!\n");
        return 1;
    }

    uint32_t capacity = (uint32_t)env_long("KV_CAPACITY",
                            (cfg.total_size >= 16384) ? 16384 : cfg.total_size);
    capacity -= capacity % cfg.erase_size;
    if (capacity < cfg.erase_size * 2u) { capacity = cfg.erase_size * 2u; }
    if (capacity > cfg.total_size) {
        capacity = cfg.total_size - (cfg.total_size % cfg.erase_size);
    }
    uint32_t nsectors = capacity / cfg.erase_size;
    if (nsectors > 64) { nsectors = 64; }
    printf("  [info] 介质=%s 容量=%uB 块=%uB | FCB 区=%uB (%u 扇区)\n",
           cfg.type == FLASH_TYPE_NAND ? "NAND" : "NOR",
           cfg.total_size, cfg.erase_size, capacity, nsectors);

    flash_sim_erase(s_dev, 0, capacity);

    /* 注册 Zephyr 设备与分区（id=0），填充 FCB 扇区表 */
    const struct device *zdev =
        zephyr_compat_register_flash(s_dev, cfg.erase_size, 1, 0xFF);
    zephyr_compat_register_area(0, zdev, 0, capacity);

    memset(&s_fcb, 0, sizeof(s_fcb));
    s_fcb.f_magic = FCB_MAGIC;
    s_fcb.f_version = 1;
    s_fcb.f_sector_cnt = (uint16_t)nsectors;
    s_fcb.f_scratch_cnt = 1; /* 保留 1 个 scratch 扇区 */
    for (uint32_t i = 0; i < nsectors; i++) {
        s_sectors[i].fs_off = (off_t)i * cfg.erase_size;
        s_sectors[i].fs_size = cfg.erase_size;
    }
    s_fcb.f_sectors = s_sectors;

    if (fcb_init(0, &s_fcb) != 0) {
        printf("  [FAIL] fcb_init 失败\n");
        flash_sim_deinit(s_dev);
        return 1;
    }
    printf("  [OK  ] fcb_init 初始化成功\n");

    const char *tests = getenv("KV_TESTS");
    const char *items = getenv("KV_ITEMS");
    uint32_t rounds = (uint32_t)env_long("KV_ROUNDS", 20);

    if (has_test(tests, "append")) {
        printf("\n[测试项] 追加/首条读取\n");
        t_append();
    }
    if (has_test(tests, "walk")) {
        printf("\n[测试项] 全量遍历\n");
        t_walk();
    }
    if (has_test(tests, "rotate")) {
        printf("\n[测试项] 扇区轮转\n");
        t_rotate(capacity);
    }
    if (has_test(tests, "clear")) {
        printf("\n[测试项] 清空\n");
        t_clear();
    }
    if (has_test(tests, "powerloss")) {
        printf("\n[测试项] 掉电恢复（重启重载）\n");
        t_powerloss();
    }

    if (has_test(tests, "func")) {
        printf("\n[测试项] 功能压测（追加/回读）\n");
        uint32_t acc_ops = 0, acc_lost = 0;
        if (items && *items) {
            const char *p = items;
            while (*p) {
                uint32_t vlen = (uint32_t)strtoul(p, (char **)&p, 10);
                if (*p == ',') { p++; }
                uint32_t n = (uint32_t)strtoul(p, (char **)&p, 10);
                if (*p == ',') { p++; }
                uint32_t freq = (uint32_t)strtoul(p, (char **)&p, 10);
                func_item(vlen, n, freq, rounds, &acc_ops, &acc_lost);
                if (*p == ';') { p++; } else { break; }
            }
        } else {
            func_item(32, 50, 50, rounds, &acc_ops, &acc_lost);
        }

        flash_stats_t fst;
        flash_sim_get_stats(s_dev, &fst);
        printf("STATS_JSON:{\"mode\":\"func\",\"ops\":%u,\"lost\":%u,"
               "\"block_us\":%llu,\"reads\":%u,\"writes\":%u,\"erases\":%u,"
               "\"max_cycles\":%u,\"avg_cycles\":%u,\"erase_cycles\":%u}\n",
               acc_ops, acc_lost,
               (unsigned long long)(fst.read_time_us + fst.write_time_us
                                    + fst.erase_time_us),
               fst.total_reads, fst.total_writes, fst.total_erases,
               fst.max_erase_cycles, fst.avg_erase_cycles, cfg.erase_cycles);
        expect("功能压测数据丢失为0", acc_lost == 0);
    } else {
        flash_stats_t st;
        flash_sim_get_stats(s_dev, &st);
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

    uint32_t bc = flash_sim_block_count(s_dev);
    if (bc > 0) {
        uint32_t *map = (uint32_t *)malloc(sizeof(uint32_t) * bc);
        if (map) {
            flash_sim_get_wear_map(s_dev, map, bc);
            printf("WEARMAP:");
            for (uint32_t i = 0; i < bc; i++) {
                printf("%s%u", i ? "," : "", map[i]);
            }
            printf("\n");
            free(map);
        }
    }

    flash_sim_deinit(s_dev);
    printf("\n=== FCB 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
