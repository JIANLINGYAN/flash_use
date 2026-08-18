/**
 * main_easyflash.c - EasyFlash(ENV/KV) 组件在模拟基座上的运行验证
 *
 * 定位：EasyFlash 是成熟开源 KV 框架（armink/EasyFlash V4.x NG 模式），
 * 自带磨损均衡（扇区轮转）、掉电保护（状态表多阶段提交）与垃圾回收。
 * 本测试通过 ef_port.c 将其对接到本平台模拟基座，验证：
 *   写入/读取、更新覆盖、删除、掉电安全、GC 触发、多类型数据、功能压测。
 *
 * 测试驱动（环境变量，与平台其它框架保持一致）：
 *   SIM_*        模拟基座参数（类型/容量/块大小/寿命/耗时/坏块）
 *   KV_CAPACITY  KV(ENV) 区容量(字节)，须 >= 2 个擦除块
 *   KV_TESTS     启用的测试项（逗号分隔）：
 *                  write_read / update / delete / powerloss / gc /
 *                  types / func
 *   KV_ITEMS     功能压测条目表：LEN,N,FREQ;LEN,N,FREQ
 *   KV_ROUNDS    功能压测轮数（默认 20）
 *   EF_VERBOSE   非 0 时打印 EasyFlash 内部日志
 *
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...
 */

#include <easyflash.h>

#include "ef_port.h"
#include "flash_hal_adapter.h"
#include "flash_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EF_BIN "easyflash_demo.bin"

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

/* 简单 LCG 随机，保证可复现 */
static uint32_t s_seed = 0x12345678u;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed;
}

static int has_test(const char *tests, const char *name)
{
    if (!tests || !*tests) { return 1; } /* 未指定则全跑 */
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
 * 重新初始化 ENV 区：擦除整个 KV 区并让 EasyFlash 重建。
 * EasyFlash 的 init_ok 是内部静态标志，仅允许初始化一次，因此这里通过
 * ef_env_set_default() 让其把区域恢复到默认（全新）状态，等价于重新格式化。
 */
static void ef_reset_area(void)
{
    ef_env_set_default();
}

/* ---------------- 基础测试项 ---------------- */

static void t_write_read(void)
{
    ef_reset_area();
    const char *v = "hello-easyflash";
    expect("write_read: set",
           ef_set_env_blob("k_hello", v, strlen(v)) == EF_NO_ERR);

    char rb[64] = {0};
    size_t saved = 0;
    size_t got = ef_get_env_blob("k_hello", rb, sizeof(rb), &saved);
    expect("write_read: get", got == strlen(v));
    expect("write_read: match", saved == strlen(v) && memcmp(rb, v, got) == 0);
}

static void t_update(void)
{
    ef_reset_area();
    ef_set_env_blob("k_up", "v1", 2);
    const char *v2 = "updated-value-longer";
    expect("update: set again",
           ef_set_env_blob("k_up", v2, strlen(v2)) == EF_NO_ERR);

    char rb[64] = {0};
    size_t saved = 0;
    ef_get_env_blob("k_up", rb, sizeof(rb), &saved);
    expect("update: 读到最新值",
           saved == strlen(v2) && memcmp(rb, v2, saved) == 0);
}

static void t_delete(void)
{
    ef_reset_area();
    uint32_t num = 0x12345678u;
    ef_set_env_blob("k_del", &num, sizeof(num));
    expect("delete: del", ef_del_env("k_del") == EF_NO_ERR);

    size_t saved = 0;
    size_t got = ef_get_env_blob("k_del", NULL, 0, &saved);
    expect("delete: 删除后读不到", got == 0);
}

/*
 * 掉电安全：EasyFlash 用"状态表 + 多阶段提交"保证任何时刻掉电后都能恢复到
 * 一致状态。这里模拟一种典型掉电：在 ENV 区尾部注入一段随机脏数据（相当于
 * 一条写入过程中被打断的残记录），随后重新加载 ENV，已提交的数据必须完好，
 * 且残记录不会被误认为有效 ENV。
 */
static void t_powerloss(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    ef_reset_area();
    const char *keep = "must-survive";
    ef_set_env_blob("k_safe", keep, strlen(keep));

    /* 在区域末块中间注入一段未完成的"脏"写入（模拟写入途中掉电） */
    uint8_t junk[16];
    for (size_t i = 0; i < sizeof(junk); i++) { junk[i] = (uint8_t)rnd(); }
    (void)flash_sim_write(dev, base + size - 64, junk, sizeof(junk));

    /* 重新加载 ENV（等价于重启后 easyflash 重新扫描） */
    expect("powerloss: 重新加载", ef_load_env() == EF_NO_ERR);

    char rb[64] = {0};
    size_t saved = 0;
    ef_get_env_blob("k_safe", rb, sizeof(rb), &saved);
    expect("powerloss: 已提交数据完好",
           saved == strlen(keep) && memcmp(rb, keep, saved) == 0);
}

/*
 * GC 验证：反复写入同一批 key 直到累计写入量远超区域容量，迫使 EasyFlash
 * 多次触发扇区回收。成熟框架应始终保持数据可读且不出现写入失败。
 */
static void t_gc(flash_dev_t *dev, uint32_t capacity)
{
    ef_reset_area();
    uint8_t big[128];
    memset(big, 0xAB, sizeof(big));

    /* 写入总量约为区容量的 4 倍，确保多轮 GC */
    uint32_t iters = (capacity / sizeof(big)) * 4u;
    if (iters < 32u) { iters = 32u; }

    uint32_t err = 0;
    for (uint32_t i = 0; i < iters; i++) {
        big[0] = (uint8_t)i;
        if (ef_set_env_blob("k_gc", big, sizeof(big)) != EF_NO_ERR) { err++; }
    }
    expect("gc: 反复写入无失败", err == 0);

    uint8_t rb[128] = {0};
    size_t saved = 0;
    ef_get_env_blob("k_gc", rb, sizeof(rb), &saved);
    expect("gc: GC 后数据正确",
           saved == sizeof(big) && rb[0] == (uint8_t)(iters - 1));

    flash_stats_t st;
    flash_sim_get_stats(dev, &st);
    printf("    [info] GC 期间累计擦除 %u 次，最高磨损 %u 次\n",
           st.total_erases, st.max_erase_cycles);
    expect("gc: 确实触发了擦除回收", st.total_erases > 0);
}

/* 多类型数据：int / float / 字符串 / 结构体 */
typedef struct {
    uint32_t id;
    int16_t  level;
    char     tag[8];
} demo_cfg_t;

static void t_types(void)
{
    ef_reset_area();

    int32_t iv = -123456;
    float   fv = 3.14159f;
    const char *sv = "string-value";
    demo_cfg_t cv = {0};
    cv.id = 0xDEADBEEF;
    cv.level = -7;
    memcpy(cv.tag, "cfgtag", 7);

    ef_set_env_blob("t_int", &iv, sizeof(iv));
    ef_set_env_blob("t_float", &fv, sizeof(fv));
    ef_set_env_blob("t_str", sv, strlen(sv));
    ef_set_env_blob("t_struct", &cv, sizeof(cv));

    int32_t iv2 = 0;
    float fv2 = 0.0f;
    char sv2[32] = {0};
    demo_cfg_t cv2;
    size_t saved = 0;
    memset(&cv2, 0, sizeof(cv2));

    ef_get_env_blob("t_int", &iv2, sizeof(iv2), &saved);
    expect("types: int", iv2 == iv);
    ef_get_env_blob("t_float", &fv2, sizeof(fv2), &saved);
    expect("types: float", fv2 == fv);
    ef_get_env_blob("t_str", sv2, sizeof(sv2), &saved);
    expect("types: string",
           saved == strlen(sv) && memcmp(sv2, sv, saved) == 0);
    ef_get_env_blob("t_struct", &cv2, sizeof(cv2), &saved);
    expect("types: struct",
           saved == sizeof(cv) && memcmp(&cv2, &cv, sizeof(cv)) == 0);
}

/* 单条目功能压测；累加统计到 acc_* */
static void func_item(uint32_t vlen, uint32_t n, uint32_t freq, uint32_t rounds,
                      uint32_t *acc_ops, uint32_t *acc_lost)
{
    ef_reset_area();
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

    /* 初始写入 n 条 */
    for (uint32_t k = 1; k <= n; k++) {
        snprintf(key, sizeof(key), "f%u", k);
        for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
        if (ef_set_env_blob(key, buf, vlen) == EF_NO_ERR) { (*acc_ops)++; }
    }

    /* 按修改频率多轮更新，并立即回读校验 */
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t k = 1; k <= n; k++) {
            snprintf(key, sizeof(key), "f%u", k);
            if ((rnd() % 100u) < freq) {
                for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
                if (ef_set_env_blob(key, buf, vlen) != EF_NO_ERR) {
                    lost++;
                    continue;
                }
                (*acc_ops)++;
                size_t saved = 0;
                size_t got = ef_get_env_blob(key, rbuf, vlen, &saved);
                if (got != vlen || memcmp(buf, rbuf, vlen) != 0) { lost++; }
            } else {
                size_t saved = 0;
                if (ef_get_env_blob(key, rbuf, vlen, &saved) > 0) {
                    (*acc_ops)++;
                }
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
    printf("=== EasyFlash(ENV/KV) 组件运行验证 ===\n");

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    cfg.erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024);
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.read_size = 1;
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = EF_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);

    /* EasyFlash 依赖按块擦除语义，EEPROM(无擦除) 不适用 */
    if (cfg.type == FLASH_TYPE_EEPROM) {
        printf("  [info] EasyFlash 依赖块擦除语义，EEPROM 模式自动切换为 NOR\n");
        cfg.type = FLASH_TYPE_NOR;
    }
    /* EasyFlash 写粒度为字节（EF_WRITE_GRAN=1），强制最小写入单位为 1 */
    if (cfg.write_size != 1) {
        printf("  [info] EasyFlash 使用字节写粒度，最小写入单位由 %u 调整为 1\n",
               cfg.write_size);
        cfg.write_size = 1;
    }

    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) {
        printf("  Flash 初始化失败!\n");
        return 1;
    }
    flash_hal_t hal;
    flash_hal_from_sim(dev, cfg.total_size, cfg.erase_size, cfg.write_size, &hal);

    /* KV 区容量：须为擦除块整数倍且至少 2 块（GC 需要空闲块） */
    uint32_t capacity = (uint32_t)env_long("KV_CAPACITY",
                            (cfg.total_size >= 16384) ? 16384 : cfg.total_size);
    capacity -= capacity % cfg.erase_size;
    if (capacity < cfg.erase_size * 2u) {
        capacity = cfg.erase_size * 2u;
    }
    if (capacity > cfg.total_size) {
        capacity = cfg.total_size - (cfg.total_size % cfg.erase_size);
    }
    printf("  [info] 介质=%s 容量=%uB 块=%uB | ENV 区=%uB (%u 块)\n",
           cfg.type == FLASH_TYPE_NAND ? "NAND" : "NOR",
           cfg.total_size, cfg.erase_size, capacity,
           capacity / cfg.erase_size);

    /* 全片擦除，保证从干净介质开始（避免历史 BIN 残留干扰） */
    flash_sim_erase(dev, 0, capacity);

    /* 注入移植层参数，然后初始化 EasyFlash */
    ef_port_setup(&hal, 0, capacity, cfg.erase_size,
                  (int)env_long("EF_VERBOSE", 0));
    if (easyflash_init() != EF_NO_ERR) {
        printf("  [FAIL] easyflash_init 失败\n");
        flash_sim_deinit(dev);
        return 1;
    }
    printf("  [OK  ] easyflash_init 初始化成功\n");

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
        printf("\n[测试项] 掉电安全（脏数据注入后重载）\n");
        t_powerloss(dev, 0, capacity);
    }
    if (has_test(tests, "gc")) {
        printf("\n[测试项] 垃圾回收（超容量反复写入）\n");
        t_gc(dev, capacity);
    }
    if (has_test(tests, "types")) {
        printf("\n[测试项] 多类型数据（int/float/string/struct）\n");
        t_types();
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

    /* 磨损分布导出（供前端绘制热力图） */
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
    printf("\n=== EasyFlash 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
