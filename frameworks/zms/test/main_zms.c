/**
 * main_zms.c - Zephyr ZMS（Zephyr Memory Storage，键值存储）组件验证
 *
 * 定位：ZMS 是 Zephyr 较新的 KV 存储引擎（固定大小槽位 + 磨损均衡 +
 * 掉电安全，定位替代 NVS），vendor 零修改，经 Zephyr 兼容层桥接 flash_sim。
 * 本测试验证：写入/读取、更新覆盖、删除、掉电安全、GC、功能压测。
 *
 * 测试驱动（环境变量，与平台其它框架保持一致）：
 *   SIM_*        模拟基座参数
 *   KV_CAPACITY  ZMS 分区容量(字节)，须为擦除块整数倍且 >= 2 块
 *   KV_TESTS     write_read/update/delete/powerloss/gc/types/func
 *   KV_ITEMS     功能压测条目表：LEN,N,FREQ;...
 *   KV_ROUNDS    压测轮数（默认 20）
 *
 * 输出：STATS_JSON:{...} 与 WEARMAP:...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kvss/zms.h>

#include "flash_hal_adapter.h"
#include "flash_sim.h"
#include "zephyr_compat.h"

#define ZMS_BIN "zms_demo.bin"

static int g_fail = 0;
static struct zms_fs s_fs;
static flash_dev_t *s_dev;

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

/* 重新挂载（模拟设备重启后重新扫描恢复） */
static int zms_remount(void)
{
    return zms_mount(&s_fs);
}

static int zms_put(zms_id_t id, const void *val, size_t len)
{
    return zms_write(&s_fs, id, val, len) >= 0 ? 0 : -1;
}

static int zms_get(zms_id_t id, void *val, size_t *len)
{
    ssize_t rc = zms_read(&s_fs, id, val, *len);

    if (rc < 0) { return -1; }
    *len = (size_t)rc;
    return 0;
}

/* ---------------- 测试项 ---------------- */

static void t_write_read(void)
{
    const char *v = "hello-zephyr-zms";
    expect("write_read: write", zms_put(0x0001, v, strlen(v)) == 0);

    char rb[64] = {0};
    size_t rl = sizeof(rb);
    expect("write_read: read", zms_get(0x0001, rb, &rl) == 0);
    expect("write_read: match",
           rl == strlen(v) && memcmp(rb, v, rl) == 0);
}

static void t_update(void)
{
    const char *v1 = "v1";
    zms_put(0x0002, v1, strlen(v1));
    const char *v2 = "updated-value-longer-zephyr-zms";
    expect("update: 再写", zms_put(0x0002, v2, strlen(v2)) == 0);

    char rb[64] = {0};
    size_t rl = sizeof(rb);
    zms_get(0x0002, rb, &rl);
    expect("update: 读到最新值",
           rl == strlen(v2) && memcmp(rb, v2, rl) == 0);
}

static void t_delete(void)
{
    uint32_t num = 0x12345678u;
    zms_put(0x0003, &num, sizeof(num));
    expect("delete: del", zms_delete(&s_fs, 0x0003) == 0);

    char rb[8] = {0};
    size_t rl = sizeof(rb);
    expect("delete: 删除后读不到", zms_get(0x0003, rb, &rl) != 0);
}

/* 掉电安全：写入已提交数据，在分区末段空白区注入"未完成写入"残留
 * （模拟写入途中掉电），重启重载后已提交数据必须完好。 */
static void t_powerloss(void)
{
    const char *keep = "must-survive-zms";
    expect("powerloss: 写入", zms_put(0x0004, keep, strlen(keep)) == 0);

    /* 在距分区末尾 2*erase_size 的空白扇区写入残留数据（非擦除值） */
    uint32_t cap = (uint32_t)env_long("KV_CAPACITY", 0);
    uint32_t erase = (uint32_t)env_long("SIM_ERASE", 4096);
    uint8_t junk[64];
    for (size_t i = 0; i < sizeof(junk); i++) { junk[i] = (uint8_t)rnd(); }
    (void)flash_sim_write(s_dev, cap - 2u * erase, junk, sizeof(junk));

    expect("powerloss: 重启重载", zms_remount() == 0);

    char rb[64] = {0};
    size_t rl = sizeof(rb);
    zms_get(0x0004, rb, &rl);
    expect("powerloss: 已提交数据完好",
           rl == strlen(keep) && memcmp(rb, keep, rl) == 0);
}

/* GC：反复写同一 id 使写入量远超分区容量，ZMS 应自动回收扇区 */
static void t_gc(uint32_t capacity)
{
    uint8_t big[128];
    memset(big, 0xAB, sizeof(big));

    uint32_t iters = (capacity / sizeof(big)) * 4u;
    if (iters < 32u) { iters = 32u; }

    uint32_t err = 0;
    for (uint32_t i = 0; i < iters; i++) {
        big[0] = (uint8_t)i;
        if (zms_put(0x0005, big, sizeof(big)) != 0) { err++; }
    }
    expect("gc: 反复写入无失败", err == 0);

    uint8_t rb[128] = {0};
    size_t rl = sizeof(rb);
    zms_get(0x0005, rb, &rl);
    expect("gc: GC 后数据正确",
           rl == sizeof(big) && rb[0] == (uint8_t)(iters - 1));

    flash_stats_t st;
    flash_sim_get_stats(s_dev, &st);
    printf("    [info] GC 期间累计擦除 %u 次\n", st.total_erases);
    expect("gc: 确实触发擦除回收", st.total_erases > 0);
}

/* 多类型数据 */
static void t_types(void)
{
    int32_t iv = -123456;
    float fv = 3.14159f;
    const char *sv = "string-value";
    zms_put(0x0010, &iv, sizeof(iv));
    zms_put(0x0011, &fv, sizeof(fv));
    zms_put(0x0012, sv, strlen(sv));

    int32_t iv2 = 0;
    float fv2 = 0.0f;
    char sv2[32] = {0};
    size_t saved = sizeof(iv2);
    zms_get(0x0010, &iv2, &saved);
    expect("types: int", iv2 == iv);
    saved = sizeof(fv2);
    zms_get(0x0011, &fv2, &saved);
    expect("types: float", fv2 == fv);
    saved = sizeof(sv2);
    zms_get(0x0012, sv2, &saved);
    expect("types: string",
           saved == strlen(sv) && memcmp(sv2, sv, saved) == 0);
}

/* 单条目功能压测 */
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
    for (uint32_t k = 1; k <= n; k++) {
        for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
        if (zms_put(k, buf, vlen) == 0) { (*acc_ops)++; }
    }

    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t k = 1; k <= n; k++) {
            if ((rnd() % 100u) < freq) {
                for (uint32_t j = 0; j < vlen; j++) { buf[j] = (uint8_t)rnd(); }
                if (zms_put(k, buf, vlen) != 0) {
                    lost++;
                    continue;
                }
                (*acc_ops)++;
                size_t rl = vlen;
                if (zms_get(k, rbuf, &rl) != 0 ||
                    rl != vlen || memcmp(buf, rbuf, vlen) != 0) { lost++; }
            } else {
                size_t rl = vlen;
                if (zms_get(k, rbuf, &rl) == 0) { (*acc_ops)++; }
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
    printf("=== Zephyr ZMS(KV) 组件运行验证 ===\n");

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    cfg.erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024);
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.read_size = 1;
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = ZMS_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);

    if (cfg.type == FLASH_TYPE_EEPROM) {
        printf("  [info] ZMS 依赖块擦除语义，EEPROM 自动切换为 NOR\n");
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
    printf("  [info] 介质=%s 容量=%uB 块=%uB | ZMS 区=%uB (%u 块)\n",
           cfg.type == FLASH_TYPE_NAND ? "NAND" : "NOR",
           cfg.total_size, cfg.erase_size, capacity, capacity / cfg.erase_size);

    flash_sim_erase(s_dev, 0, capacity);

    /* 打开介质 -> 包装为统一 flash_hal_t -> 注册给兼容层 */
    flash_hal_t hal;
    flash_hal_from_sim(s_dev, cfg.total_size, cfg.erase_size, cfg.write_size, &hal);

    const struct device *zdev =
        zephyr_compat_register_flash(&hal, cfg.erase_size, 1, 0xFF);
    memset(&s_fs, 0, sizeof(s_fs));
    s_fs.offset = 0;
    s_fs.sector_size = cfg.erase_size;
    s_fs.sector_count = capacity / cfg.erase_size;
    s_fs.flash_device = zdev;
    if (zms_mount(&s_fs) != 0) {
        printf("  [FAIL] zms_mount 失败\n");
        flash_sim_deinit(s_dev);
        return 1;
    }
    printf("  [OK  ] zms_mount 初始化成功\n");

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
        printf("\n[测试项] 掉电安全（脏数据注入后重启重载）\n");
        t_powerloss();
    }
    if (has_test(tests, "gc")) {
        printf("\n[测试项] 垃圾回收（超容量反复写入）\n");
        t_gc(capacity);
    }
    if (has_test(tests, "types")) {
        printf("\n[测试项] 多类型数据（int/float/string）\n");
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
    printf("\n=== ZMS 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
