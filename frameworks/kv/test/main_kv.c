/**
 * main_kv.c - KV 存储逻辑框架运行验证
 *
 * 两种模式（环境变量切换）：
 *  默认：一致性自检（写入/读取/更新/删除/掉电残留丢弃/GC）。
 *  KV_FUNC=1：功能压测模式，可配置：
 *      KV_CAPACITY  KV 区容量(字节)
 *      KV_N         条目数量
 *      KV_VLEN      每条 value 长度(字节)
 *      KV_ROUNDS    修改轮数（每轮随机更新+读取）
 *      KV_MODFREQ   每轮修改比例(0~100)，其余为读取
 *  输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...
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

/* -------- 默认：一致性自检（保留原验证） -------- */
static int self_check(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    expect("kv_init", kv_init(dev, base, size) == FLASH_OK);

    const char *v1 = "hello-kv";
    expect("kv_write key1", kv_write(dev, 1, v1, (uint16_t)strlen(v1)) == FLASH_OK);
    char rb[32] = {0}; uint16_t rlen = sizeof(rb);
    expect("kv_read key1", kv_read(dev, 1, rb, &rlen) == FLASH_OK);
    expect("kv_read 内容一致", rlen == strlen(v1) && memcmp(rb, v1, rlen) == 0);

    int32_t num = 0x12345678;
    expect("kv_write key2(int)", kv_write(dev, 2, &num, sizeof(num)) == FLASH_OK);
    int32_t rnum = 0; uint16_t rn = sizeof(rnum);
    expect("kv_read key2", kv_read(dev, 2, &rnum, &rn) == FLASH_OK);
    expect("kv_read int 一致", rn == sizeof(num) && rnum == num);

    const char *v1b = "updated!!";
    expect("kv_write key1(更新)", kv_write(dev, 1, v1b, (uint16_t)strlen(v1b)) == FLASH_OK);
    char rb2[32] = {0}; uint16_t rlen2 = sizeof(rb2);
    expect("kv_read key1(最新)", kv_read(dev, 1, rb2, &rlen2) == FLASH_OK);
    expect("kv_read 最新一致", rlen2 == strlen(v1b) && memcmp(rb2, v1b, rlen2) == 0);

    expect("kv_delete key2", kv_delete(dev, 2) == FLASH_OK);
    uint16_t dl = 4;
    expect("kv_read key2(已删)", kv_read(dev, 2, NULL, &dl) == FLASH_ERR_ARGS);

    /* 掉电残留丢弃：注入一笔 PENDING 残记录，加载后应忽略 */
    kv_header_t h; h.magic = KV_MAGIC; h.key_id = 99; h.len = 3; h.crc = 0;
    flash_sim_write(dev, base + size - 32, &h, sizeof(h));

    /* 压实触发：反复写同一 key 直到 GC */
    char big[200]; memset(big, 0xAB, sizeof(big));
    for (int i = 0; i < 200; i++) {
        if (kv_write(dev, 7, big, (uint16_t)sizeof(big)) != FLASH_OK) break;
    }
    uint16_t blen = sizeof(big); char bback[sizeof(big)] = {0};
    expect("压实后 kv_read key7", kv_read(dev, 7, bback, &blen) == FLASH_OK);
    expect("压实后 内容一致", blen == sizeof(big) && memcmp(bback, big, blen) == 0);
    return 0;
}

/* -------- 功能压测模式 -------- */
static int func_test(flash_dev_t *dev, uint32_t base, uint32_t size)
{
    uint32_t n = (uint32_t)env_long("KV_N", 50);
    uint32_t vlen = (uint32_t)env_long("KV_VLEN", 32);
    uint32_t rounds = (uint32_t)env_long("KV_ROUNDS", 20);
    uint32_t modfreq = (uint32_t)env_long("KV_MODFREQ", 50); /* 0~100 */
    if (vlen > KV_MAX_VALUE) vlen = KV_MAX_VALUE;
    if (n == 0) n = 1;
    if (modfreq > 100) modfreq = 100;

    expect("kv_init", kv_init(dev, base, size) == FLASH_OK);

    /* 简易确定性随机（LCG），便于复现 */
    uint32_t seed = 0x1234u;
    #define RND() (seed = seed * 1664525u + 1013904223u)

    uint8_t *buf = (uint8_t *)malloc(vlen ? vlen : 1);
    uint8_t *rbuf = (uint8_t *)malloc(vlen ? vlen : 1);
    uint32_t lost = 0;
    uint32_t ops = 0;

    /* 初始写入 N 条 */
    for (uint32_t k = 1; k <= n; k++) {
        for (uint32_t j = 0; j < vlen; j++) buf[j] = (uint8_t)RND();
        if (kv_write(dev, (uint16_t)k, buf, (uint16_t)vlen) != FLASH_OK) {
            expect("func initial write", 0);
        }
        ops++;
    }

    /* 多轮修改/读取 */
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t k = 1; k <= n; k++) {
            uint32_t pick = RND() % 100;
            if (pick < modfreq) {
                /* 修改 */
                for (uint32_t j = 0; j < vlen; j++) buf[j] = (uint8_t)RND();
                if (kv_write(dev, (uint16_t)k, buf, (uint16_t)vlen) != FLASH_OK) {
                    /* 可能触发 GC 后成功，重试一次 */
                    kv_write(dev, (uint16_t)k, buf, (uint16_t)vlen);
                }
                ops++;
                /* 立即读回校验数据丢失 */
                uint16_t rl = (uint16_t)vlen;
                if (kv_read(dev, (uint16_t)k, rbuf, &rl) == FLASH_OK) {
                    if (rl != vlen || memcmp(buf, rbuf, vlen) != 0) lost++;
                }
            } else {
                uint16_t rl = (uint16_t)vlen;
                if (kv_read(dev, (uint16_t)k, rbuf, &rl) == FLASH_OK) ops++;
            }
        }
    }
    free(buf); free(rbuf);

    /* 统计：从基座取性能与磨损 */
    flash_stats_t st;
    flash_sim_get_stats(dev, &st);
    printf("STATS_JSON:{\"mode\":\"func\",\"entries\":%u,\"vlen\":%u,"
           "\"rounds\":%u,\"modfreq\":%u,\"ops\":%u,\"lost\":%u,"
           "\"block_us\":%llu,\"reads\":%u,\"writes\":%u,\"erases\":%u}\n",
           n, vlen, rounds, modfreq, ops, lost,
           (unsigned long long)(st.read_time_us + st.write_time_us + st.erase_time_us),
           st.total_reads, st.total_writes, st.total_erases);

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
    expect("功能测试数据丢失为0", lost == 0);
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
    /* KV 区需擦除后使用 */
    flash_sim_erase(dev, 0, capacity);

    if (env_long("KV_FUNC", 0) == 1) {
        func_test(dev, 0, capacity);
    } else {
        self_check(dev, 0, capacity);
        printf("  [info] 性能与磨损统计：\n");
        flash_stats_t st; flash_sim_get_stats(dev, &st);
        printf("STATS_JSON:{\"reads\":%u,\"writes\":%u,\"erases\":%u,"
               "\"write_bytes\":%u,\"max_cycles\":%u,\"avg_cycles\":%u,"
               "\"read_us\":%llu,\"write_us\":%llu,\"erase_us\":%llu,"
               "\"bad_blocks\":%u}\n",
               st.total_reads, st.total_writes, st.total_erases,
               st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
               (unsigned long long)st.read_time_us,
               (unsigned long long)st.write_time_us,
               (unsigned long long)st.erase_time_us, st.bad_block_count);
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
    }

    flash_sim_deinit(dev);
    printf("\n=== KV 运行验证结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
