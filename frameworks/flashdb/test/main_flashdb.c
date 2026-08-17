/**
 * main_flashdb.c - FlashDB(KVDB) 组件在模拟基座上的运行验证
 *
 * 定位：FlashDB 是 EasyFlash 作者的下一代作品（armink/FlashDB），
 * KVDB 部分同样具备磨损均衡、掉电保护与垃圾回收，并支持 blob 接口。
 * 本测试通过 FAL 移植层（fal_flash_sim_port.c）将其对接到本平台模拟基座。
 *
 * 测试驱动（环境变量，与平台其它框架保持一致）：
 *   SIM_*        模拟基座参数（类型/容量/块大小/寿命/耗时/坏块）
 *   KV_CAPACITY  KVDB 分区容量(字节)，须 >= 2 个擦除块
 *   KV_TESTS     启用的测试项（逗号分隔）：
 *                  write_read / update / delete / powerloss / gc /
 *                  types / iterate / func
 *   KV_ITEMS     功能压测条目表：LEN,N,FREQ;LEN,N,FREQ
 *   KV_ROUNDS    功能压测轮数（默认 20）
 *   FDB_VERBOSE  非 0 时打印 FlashDB 内部日志
 *
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...
 */

#include <fal.h>
#include <flashdb.h>

#include "fal_flash_sim_port.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FDB_BIN "flashdb_demo.bin"

static int g_fail = 0;

/* KVDB 实例与默认 KV 集合 */
static struct fdb_kvdb s_kvdb;
static struct fdb_default_kv_node s_default_kv_table[] = {
    {"fdb_ver", (void *)"2.x", 3},
};

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

static uint32_t s_seed = 0x2468ACEu;
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

/* 便捷封装：设置一个 blob KV */
static fdb_err_t kv_set(const char *key, const void *val, size_t len)
{
    struct fdb_blob blob;
    return fdb_kv_set_blob(&s_kvdb, key,
                           fdb_blob_make(&blob, val, len));
}

/* 便捷封装：读取一个 blob KV，返回实际读取长度 */
static size_t kv_get(const char *key, void *buf, size_t len, size_t *saved)
{
    struct fdb_blob blob;
    size_t got = fdb_kv_get_blob(&s_kvdb, key,
                                 fdb_blob_make(&blob, buf, len));
    if (saved) { *saved = blob.saved.len; }
    return got;
}

/* 恢复 KVDB 到出厂（等价重新格式化分区），保证各测试项互不干扰 */
static void kv_reset(void)
{
    fdb_kv_set_default(&s_kvdb);
}

/* ---------------- 基础测试项 ---------------- */

static void t_write_read(void)
{
    kv_reset();
    const char *v = "hello-flashdb";
    expect("write_read: set", kv_set("k_hello", v, strlen(v)) == FDB_NO_ERR);

    char rb[64] = {0};
    size_t saved = 0;
    size_t got = kv_get("k_hello", rb, sizeof(rb), &saved);
    expect("write_read: get", got == strlen(v));
    expect("write_read: match",
           saved == strlen(v) && memcmp(rb, v, got) == 0);
}

static void t_update(void)
{
    kv_reset();
    kv_set("k_up", "v1", 2);
    const char *v2 = "updated-value-longer";
    expect("update: set again", kv_set("k_up", v2, strlen(v2)) == FDB_NO_ERR);

    char rb[64] = {0};
    size_t saved = 0;
    kv_get("k_up", rb, sizeof(rb), &saved);
    expect("update: 读到最新值",
           saved == strlen(v2) && memcmp(rb, v2, saved) == 0);
}

static void t_delete(void)
{
    kv_reset();
    uint32_t num = 0x12345678u;
    kv_set("k_del", &num, sizeof(num));
    expect("delete: del", fdb_kv_del(&s_kvdb, "k_del") == FDB_NO_ERR);

    uint32_t back = 0;
    size_t saved = 0;
    size_t got = kv_get("k_del", &back, sizeof(back), &saved);
    expect("delete: 删除后读不到", got == 0);
}

/*
 * 掉电安全：FlashDB 以"状态位 + 多阶段提交"保证掉电一致性。
 * 这里在分区尾部注入一段未完成的脏写入（模拟写入途中掉电），
 * 随后重新挂载 KVDB，已提交数据必须完好且脏数据不被误判为有效 KV。
 */
static void t_powerloss(flash_dev_t *dev, uint32_t base, uint32_t size,
                        uint32_t erase_size, uint32_t total)
{
    kv_reset();
    const char *keep = "must-survive";
    kv_set("k_safe", keep, strlen(keep));

    uint8_t junk[16];
    for (size_t i = 0; i < sizeof(junk); i++) { junk[i] = (uint8_t)rnd(); }
    (void)flash_sim_write(dev, base + size - 64, junk, sizeof(junk));

    /* 重新挂载 KVDB（等价于重启后重新扫描分区） */
    fdb_kvdb_deinit(&s_kvdb);
    memset(&s_kvdb, 0, sizeof(s_kvdb));
    fdb_err_t r = fdb_kvdb_init(&s_kvdb, "sim_kv", FAL_KVDB_PART_NAME, NULL, NULL);
    expect("powerloss: 重新挂载", r == FDB_NO_ERR);
    (void)erase_size;
    (void)total;

    char rb[64] = {0};
    size_t saved = 0;
    kv_get("k_safe", rb, sizeof(rb), &saved);
    expect("powerloss: 已提交数据完好",
           saved == strlen(keep) && memcmp(rb, keep, saved) == 0);
}

/* GC 验证：写入量远超分区容量，迫使多轮扇区回收 */
static void t_gc(flash_dev_t *dev, uint32_t capacity)
{
    kv_reset();
    uint8_t big[128];
    memset(big, 0xCD, sizeof(big));

    uint32_t iters = (capacity / sizeof(big)) * 4u;
    if (iters < 32u) { iters = 32u; }

    uint32_t err = 0;
    for (uint32_t i = 0; i < iters; i++) {
        big[0] = (uint8_t)i;
        if (kv_set("k_gc", big, sizeof(big)) != FDB_NO_ERR) { err++; }
    }
    expect("gc: 反复写入无失败", err == 0);

    uint8_t rb[128] = {0};
    size_t saved = 0;
    kv_get("k_gc", rb, sizeof(rb), &saved);
    expect("gc: GC 后数据正确",
           saved == sizeof(big) && rb[0] == (uint8_t)(iters - 1));

    flash_stats_t st;
    flash_sim_get_stats(dev, &st);
    printf("    [info] GC 期间累计擦除 %u 次，最高磨损 %u 次\n",
           st.total_erases, st.max_erase_cycles);
    expect("gc: 确实触发了擦除回收", st.total_erases > 0);
}

/* 多类型数据 */
typedef struct {
    uint32_t id;
    int16_t  level;
    char     tag[8];
} demo_cfg_t;

static void t_types(void)
{
    kv_reset();

    int32_t iv = -123456;
    float   fv = 2.71828f;
    const char *sv = "string-value";
    demo_cfg_t cv;
    memset(&cv, 0, sizeof(cv));
    cv.id = 0xCAFEBABE;
    cv.level = -9;
    memcpy(cv.tag, "fdbtag", 7);

    kv_set("t_int", &iv, sizeof(iv));
    kv_set("t_float", &fv, sizeof(fv));
    kv_set("t_str", sv, strlen(sv));
    kv_set("t_struct", &cv, sizeof(cv));

    int32_t iv2 = 0;
    float fv2 = 0.0f;
    char sv2[32] = {0};
    demo_cfg_t cv2;
    size_t saved = 0;
    memset(&cv2, 0, sizeof(cv2));

    kv_get("t_int", &iv2, sizeof(iv2), &saved);
    expect("types: int", iv2 == iv);
    kv_get("t_float", &fv2, sizeof(fv2), &saved);
    expect("types: float", fv2 == fv);
    kv_get("t_str", sv2, sizeof(sv2), &saved);
    expect("types: string",
           saved == strlen(sv) && memcmp(sv2, sv, saved) == 0);
    kv_get("t_struct", &cv2, sizeof(cv2), &saved);
    expect("types: struct",
           saved == sizeof(cv) && memcmp(&cv2, &cv, sizeof(cv)) == 0);
}

/* 迭代遍历：FlashDB 特有的 KV 迭代器 */
static void t_iterate(void)
{
    kv_reset();
    kv_set("it_a", "1", 1);
    kv_set("it_b", "2", 1);
    kv_set("it_c", "3", 1);

    struct fdb_kv_iterator it;
    fdb_kv_iterator_init(&s_kvdb, &it);
    uint32_t found = 0;
    while (fdb_kv_iterate(&s_kvdb, &it)) {
        if (strncmp(it.curr_kv.name, "it_", 3) == 0) { found++; }
    }
    expect("iterate: 遍历到 3 个 KV", found == 3);
}

/* 单条目功能压测 */
static void func_item(uint32_t vlen, uint32_t n, uint32_t freq, uint32_t rounds,
                      uint32_t *acc_ops, uint32_t *acc_lost)
{
    kv_reset();
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
    char key[16];

    for (uint32_t k = 1; k <= n; k++) {
        snprintf(key, sizeof(key), "f%u", k);
        for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
        if (kv_set(key, buf, vlen) == FDB_NO_ERR) { (*acc_ops)++; }
    }

    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t k = 1; k <= n; k++) {
            snprintf(key, sizeof(key), "f%u", k);
            if ((rnd() % 100u) < freq) {
                for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
                if (kv_set(key, buf, vlen) != FDB_NO_ERR) {
                    lost++;
                    continue;
                }
                (*acc_ops)++;
                size_t saved = 0;
                size_t got = kv_get(key, rbuf, vlen, &saved);
                if (got != vlen || memcmp(buf, rbuf, vlen) != 0) { lost++; }
            } else {
                size_t saved = 0;
                if (kv_get(key, rbuf, vlen, &saved) > 0) { (*acc_ops)++; }
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
    printf("=== FlashDB(KVDB) 组件运行验证 ===\n");

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    cfg.erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024);
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.read_size = 1;
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = FDB_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);

    if (cfg.type == FLASH_TYPE_EEPROM) {
        printf("  [info] FlashDB 依赖块擦除语义，EEPROM 模式自动切换为 NOR\n");
        cfg.type = FLASH_TYPE_NOR;
    }
    if (cfg.write_size != 1) {
        printf("  [info] FlashDB 使用字节写粒度，最小写入单位由 %u 调整为 1\n",
               cfg.write_size);
        cfg.write_size = 1;
    }

    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) {
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
    printf("  [info] 介质=%s 容量=%uB 块=%uB | KVDB 分区=%uB (%u 块)\n",
           cfg.type == FLASH_TYPE_NAND ? "NAND" : "NOR",
           cfg.total_size, cfg.erase_size, capacity,
           capacity / cfg.erase_size);

    /* 从干净介质开始 */
    flash_sim_erase(dev, 0, capacity);

    int verbose = (int)env_long("FDB_VERBOSE", 0);
    int pr = fal_sim_port_init(dev, cfg.total_size, cfg.erase_size,
                              0, capacity, verbose);
    if (pr != 0) {
        printf("  [FAIL] FAL 初始化失败 (rc=%d)\n", pr);
        flash_sim_deinit(dev);
        return 1;
    }
    printf("  [OK  ] FAL 初始化成功（分区 %s）\n", FAL_KVDB_PART_NAME);

    /* 挂载 KVDB */
    struct fdb_default_kv default_kv;
    default_kv.kvs = s_default_kv_table;
    default_kv.num = sizeof(s_default_kv_table) / sizeof(s_default_kv_table[0]);

    memset(&s_kvdb, 0, sizeof(s_kvdb));
    fdb_err_t ir = fdb_kvdb_init(&s_kvdb, "sim_kv", FAL_KVDB_PART_NAME,
                                 &default_kv, NULL);
    if (ir != FDB_NO_ERR) {
        printf("  [FAIL] fdb_kvdb_init 失败 (rc=%d)\n", (int)ir);
        flash_sim_deinit(dev);
        return 1;
    }
    printf("  [OK  ] fdb_kvdb_init 初始化成功\n");

    const char *tests = getenv("KV_TESTS");
    const char *items = getenv("KV_ITEMS");
    uint32_t rounds = (uint32_t)env_long("KV_ROUNDS", 20);

    if (has_test(tests, "write_read")) {
        printf("\n[测试项] 基础写入/读取\n");
        t_write_read();
    }
    if (has_test(tests, "update")) {
        printf("\n[测试项] 更新覆盖\n");
        t_update();
    }
    if (has_test(tests, "delete")) {
        printf("\n[测试项] 删除\n");
        t_delete();
    }
    if (has_test(tests, "powerloss")) {
        printf("\n[测试项] 掉电安全（脏数据注入后重挂载）\n");
        t_powerloss(dev, 0, capacity, cfg.erase_size, cfg.total_size);
    }
    if (has_test(tests, "gc")) {
        printf("\n[测试项] 垃圾回收（超容量反复写入）\n");
        t_gc(dev, capacity);
    }
    if (has_test(tests, "types")) {
        printf("\n[测试项] 多类型数据（int/float/string/struct）\n");
        t_types();
    }
    if (has_test(tests, "iterate")) {
        printf("\n[测试项] KV 迭代遍历\n");
        t_iterate();
    }

    if (has_test(tests, "func")) {
        printf("\n[测试项] 功能压测（条目表）\n");
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
        flash_sim_get_stats(dev, &fst);
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

    fdb_kvdb_deinit(&s_kvdb);
    flash_sim_deinit(dev);
    printf("\n=== FlashDB 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
