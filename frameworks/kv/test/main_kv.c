/**
 * main_kv.c - KV 存储逻辑框架运行验证
 *
 * 测试驱动（环境变量）：
 *   KV_CAPACITY   KV 区容量(字节)
 *   KV_TESTS      启用的测试项（逗号分隔），可勾选：
 *                   write_read 基础写入/读取
 *                   update     更新覆盖
 *                   delete     删除
 *                   powerloss  掉电残留丢弃
 *                   gc         压实垃圾回收
 *                   func       功能压测（按条目表累加执行）
 *                 不设置则运行全部基础项。
 *   KV_ITEMS      功能压测条目表（func 项使用），格式：
 *                   LEN,N,FREQ;LEN,N,FREQ
 *                   LEN=value长度 N=条目数 FREQ=每轮修改比例(0~100)
 *                 不设置则使用一条默认条目。
 *   KV_ROUNDS     功能压测每条目修改轮数（默认 20）
 *
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...
 */

#include "flash_sim.h"
#include "kv_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KV_BIN   "kv_demo.bin"

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

/* 简单 LCG 随机 */
static uint32_t s_seed;
static uint32_t rnd(void) { s_seed = s_seed * 1664525u + 1013904223u; return s_seed; }

/* 基础测试项 */
static void t_write_read(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    kv_init(dev, base, size);
    const char *v = "hello-kv";
    expect("write_read: write", kv_write(dev, 1, v, (uint16_t)strlen(v)) == FLASH_OK);
    char rb[32] = {0}; uint16_t rl = sizeof(rb);
    expect("write_read: read", kv_read(dev, 1, rb, &rl) == FLASH_OK);
    expect("write_read: match", rl == strlen(v) && memcmp(rb, v, rl) == 0);
}

static void t_update(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    kv_init(dev, base, size);
    const char *v = "v1";
    kv_write(dev, 1, v, (uint16_t)strlen(v));
    const char *v2 = "updated-value!!";
    expect("update: write", kv_write(dev, 1, v2, (uint16_t)strlen(v2)) == FLASH_OK);
    char rb[32] = {0}; uint16_t rl = sizeof(rb);
    expect("update: read latest", kv_read(dev, 1, rb, &rl) == FLASH_OK);
    expect("update: match", rl == strlen(v2) && memcmp(rb, v2, rl) == 0);
}

static void t_delete(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    kv_init(dev, base, size);
    int32_t num = 0x12345678;
    kv_write(dev, 2, &num, sizeof(num));
    expect("delete: delete", kv_delete(dev, 2) == FLASH_OK);
    uint16_t dl = 4;
    expect("delete: read-deleted", kv_read(dev, 2, NULL, &dl) == FLASH_ERR_ARGS);
}

static void t_powerloss(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    kv_init(dev, base, size);
    /* 注入一笔 PENDING 残记录（状态字未提交），加载时应忽略 */
    kv_header_t h; h.magic = KV_MAGIC; h.key_id = 99; h.len = 3; h.crc = 0;
    flash_sim_write(dev, base + size - 32, &h, sizeof(h));
    kv_init(dev, base, size); /* 重新加载，99 不应出现 */
    uint16_t dl = 8;
    expect("powerloss: 残留丢弃", kv_read(dev, 99, NULL, &dl) == FLASH_ERR_ARGS);
}

static void t_gc(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    kv_init(dev, base, size);
    char big[200]; memset(big, 0xAB, sizeof(big));
    for (int i = 0; i < 200; i++) {
        if (kv_write(dev, 7, big, (uint16_t)sizeof(big)) != FLASH_OK) break;
    }
    uint16_t blen = sizeof(big); char bback[sizeof(big)] = {0};
    expect("gc: read after gc", kv_read(dev, 7, bback, &blen) == FLASH_OK);
    expect("gc: match", blen == sizeof(big) && memcmp(bback, big, blen) == 0);
}

/* 单条目功能压测；累加统计到 *acc */
static void func_item(flash_dev_t *dev, uint32_t base, uint32_t size,
                      uint32_t vlen, uint32_t n, uint32_t freq, uint32_t rounds,
                      uint32_t *acc_ops, uint32_t *acc_lost)
{
    kv_init(dev, base, size);
    if (vlen > KV_MAX_VALUE) vlen = KV_MAX_VALUE;
    if (n == 0) n = 1;
    if (freq > 100) freq = 100;

    uint8_t *buf = (uint8_t *)malloc(vlen ? vlen : 1);
    uint8_t *rbuf = (uint8_t *)malloc(vlen ? vlen : 1);
    uint32_t lost = 0;

    for (uint32_t k = 1; k <= n; k++) {
        for (uint32_t j = 0; j < vlen; j++) buf[j] = (uint8_t)rnd();
        kv_write(dev, (uint16_t)k, buf, (uint16_t)vlen);
        (*acc_ops)++;
    }
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t k = 1; k <= n; k++) {
            if ((rnd() % 100) < freq) {
                for (uint32_t j = 0; j < vlen; j++) buf[j] = (uint8_t)rnd();
                kv_write(dev, (uint16_t)k, buf, (uint16_t)vlen);
                (*acc_ops)++;
                uint16_t rl = (uint16_t)vlen;
                if (kv_read(dev, (uint16_t)k, rbuf, &rl) == FLASH_OK) {
                    if (rl != vlen || memcmp(buf, rbuf, vlen) != 0) lost++;
                }
            } else {
                uint16_t rl = (uint16_t)vlen;
                if (kv_read(dev, (uint16_t)k, rbuf, &rl) == FLASH_OK) (*acc_ops)++;
            }
        }
    }
    free(buf); free(rbuf);
    *acc_lost += lost;
    printf("    [info] 条目(长度=%u,条数=%u,频率=%u%%): 数据丢失=%u\n",
           vlen, n, freq, lost);
}

static int has_test(const char *tests, const char *name)
{
    if (!tests || !*tests) return 1; /* 未指定则全跑 */
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

int main(void)
{
    printf("=== KV 存储逻辑框架运行验证 ===\n");

    flash_config_t cfg = {
        .type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR),
        .total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024),
        .erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024),
        .write_size = (uint32_t)env_long("SIM_WRITE", 1),
        .read_size = 1,
        .erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000),
        .bin_path = KV_BIN,
        .read_us = (uint32_t)env_long("SIM_RD_US", 0),
        .write_us = (uint32_t)env_long("SIM_WR_US", 0),
        .erase_us = (uint32_t)env_long("SIM_ERASE_US", 0),
        .bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0),
        .bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0),
    };
    flash_dev_t *dev = flash_sim_init(&cfg);
    if (!dev) { printf("  Flash 初始化失败!\n"); return 1; }

    uint32_t capacity = (uint32_t)env_long("KV_CAPACITY",
                                (cfg.total_size >= 8192) ? 8192 : cfg.total_size);
    flash_sim_erase(dev, 0, capacity);

    const char *tests = getenv("KV_TESTS");
    const char *items = getenv("KV_ITEMS");
    uint32_t rounds = (uint32_t)env_long("KV_ROUNDS", 20);

    if (has_test(tests, "write_read")) { printf("\n[测试项] 基础写入/读取\n"); t_write_read(dev, 0, capacity); }
    if (has_test(tests, "update"))     { printf("\n[测试项] 更新覆盖\n"); t_update(dev, 0, capacity); }
    if (has_test(tests, "delete"))     { printf("\n[测试项] 删除\n"); t_delete(dev, 0, capacity); }
    if (has_test(tests, "powerloss"))  { printf("\n[测试项] 掉电残留丢弃\n"); t_powerloss(dev, 0, capacity); }
    if (has_test(tests, "gc"))         { printf("\n[测试项] 压实 GC\n"); t_gc(dev, 0, capacity); }

    if (has_test(tests, "func")) {
        printf("\n[测试项] 功能压测（条目表）\n");
        uint32_t acc_ops = 0, acc_lost = 0;
        if (items && *items) {
            const char *p = items;
            while (*p) {
                /* 解析 LEN,N,FREQ */
                uint32_t vlen = (uint32_t)strtoul(p, (char **)&p, 10);
                if (*p == ',') p++;
                uint32_t n = (uint32_t)strtoul(p, (char **)&p, 10);
                if (*p == ',') p++;
                uint32_t freq = (uint32_t)strtoul(p, (char **)&p, 10);
                func_item(dev, 0, capacity, vlen, n, freq, rounds, &acc_ops, &acc_lost);
                if (*p == ';') p++; else break;
            }
        } else {
            func_item(dev, 0, capacity, 32, 50, 50, rounds, &acc_ops, &acc_lost);
        }
        flash_stats_t fst; flash_sim_get_stats(dev, &fst);
        printf("STATS_JSON:{\"mode\":\"func\",\"ops\":%u,\"lost\":%u,"
               "\"block_us\":%llu,\"reads\":%u,\"writes\":%u,\"erases\":%u}\n",
               acc_ops, acc_lost,
               (unsigned long long)(fst.read_time_us + fst.write_time_us + fst.erase_time_us),
               fst.total_reads, fst.total_writes, fst.total_erases);
        expect("功能压测数据丢失为0", acc_lost == 0);
    } else {
        /* 非功能压测模式也输出基础统计 */
        flash_stats_t st; flash_sim_get_stats(dev, &st);
        printf("STATS_JSON:{\"mode\":\"basic\",\"reads\":%u,\"writes\":%u,"
               "\"erases\":%u,\"write_bytes\":%u,\"max_cycles\":%u,"
               "\"avg_cycles\":%u,\"read_us\":%llu,\"write_us\":%llu,"
               "\"erase_us\":%llu,\"bad_blocks\":%u}\n",
               st.total_reads, st.total_writes, st.total_erases,
               st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
               (unsigned long long)st.read_time_us,
               (unsigned long long)st.write_time_us,
               (unsigned long long)st.erase_time_us, st.bad_block_count);
    }

    uint32_t bc = flash_sim_block_count(dev);
    if (bc > 0) {
        uint32_t *map = (uint32_t *)malloc(sizeof(uint32_t) * bc);
        if (map) {
            flash_sim_get_wear_map(dev, map, bc);
            printf("WEARMAP:");
            for (uint32_t i = 0; i < bc; i++) printf("%s%u", i ? "," : "", map[i]);
            printf("\n");
            free(map);
        }
    }

    flash_sim_deinit(dev);
    printf("\n=== KV 运行验证结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
