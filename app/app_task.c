/**
 * app_task.c - 应用层测试任务引擎实现
 *
 * 任务执行模型：
 *   1. 从环境变量 APP_* 读取测试选项（数据项数/长度/轮数/频率/容量）；
 *   2. 调用适配层 comp->init() 初始化组件（含底层模拟基座）；
 *   3. 按任务类型 + 组件类别执行目标操作，应用层计时；
 *   4. 从模拟基座取介质统计，计算写放大/吞吐/磨损并输出 STATS_JSON。
 *
 * 任务通过适配层的三类语义接口（kv_set/kv_get、fs_write/fs_read、
 * bm_save/bm_load）统一调用组件层，因此不同组件（自研/开源）只需
 * 实现适配器即可被同一任务引擎评估。
 */

/* setenv/unsetenv 需 POSIX 声明（与 -std=c99 严格模式兼容） */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "app_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_util.h"

/* ---------- 任务名表 ---------- */

const char *const app_task_names[APP_TASK_COUNT] = {
    "write",       /* APP_TASK_WRITE */
    "read",        /* APP_TASK_READ */
    "update",      /* APP_TASK_UPDATE */
    "durability",  /* APP_TASK_DURABILITY */
    "powerloss",   /* APP_TASK_POWERLOSS */
    "mixed",       /* APP_TASK_MIXED */
};

app_task_id_t app_task_parse(const char *name)
{
    int i;
    if (!name) {
        return APP_TASK_COUNT;
    }
    for (i = 0; i < APP_TASK_COUNT; i++) {
        if (strcmp(name, app_task_names[i]) == 0) {
            return (app_task_id_t)i;
        }
    }
    return APP_TASK_COUNT;
}

/* ---------- 内部工具 ---------- */

/* 生成确定性测试数据 */
static void fill_buf(uint8_t *buf, uint32_t len, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)((seed + i * 7u + i / 13u) & 0xFF);
    }
}

/* 生成 KV key / FS 文件名 */
static void make_name(char *out, size_t out_sz, const char *prefix, uint32_t idx)
{
    snprintf(out, out_sz, "%s%u", prefix, idx);
}

/* 从模拟基座收集介质统计并填充结果 */
static void collect_media(const app_component_t *comp, app_result_t *r)
{
    flash_dev_t *dev = comp->device();
    if (!dev) {
        return;
    }
    flash_stats_t st;
    memset(&st, 0, sizeof(st));
    flash_sim_get_stats(dev, &st);
    r->reads = st.total_reads;
    r->writes = st.total_writes;
    r->erases = st.total_erases;
    r->media_bytes = st.total_write_bytes;
    r->max_cycles = st.max_erase_cycles;
    r->avg_cycles = st.avg_erase_cycles;
    r->bad_blocks = st.bad_block_count;
    r->block_us = st.read_time_us + st.write_time_us + st.erase_time_us;
}

/* 应用层性能计算与结果输出 */
static void report_result(const char *mode, const app_component_t *comp,
                          app_result_t *r)
{
    collect_media(comp, r);
    app_print_stats(mode, r);
    app_print_wearmap(comp->device());
}

/* 读回校验通用封装（KV 语义） */
static void kv_verify(const app_component_t *comp, const char *key,
                      const uint8_t *expect, uint16_t len, app_result_t *r)
{
    uint8_t *rb = (uint8_t *)malloc(len ? len : 1);
    if (!rb) {
        r->lost++;
        return;
    }
    uint16_t rl = len;
    if (comp->kv_get(key, rb, &rl) != 0 || rl != len ||
        memcmp(rb, expect, len) != 0) {
        r->lost++;
    }
    free(rb);
}

/* 读回校验通用封装（FS 语义） */
static void fs_verify(const app_component_t *comp, const char *name,
                      const uint8_t *expect, uint32_t len, app_result_t *r)
{
    uint32_t sz = 0;
    if (comp->fs_get_size(name, &sz) != 0 || sz != len) {
        r->lost++;
        return;
    }
    uint8_t *rb = (uint8_t *)malloc(len ? len : 1);
    if (!rb) {
        r->lost++;
        return;
    }
    uint32_t rl = len;
    if (comp->fs_read(name, rb, &rl) != 0 || rl != len ||
        memcmp(rb, expect, len) != 0) {
        r->lost++;
    }
    free(rb);
}

/* ---------- 各任务实现（按组件类别分发） ---------- */

/* 任务：写入性能 */
static int task_write(const app_component_t *comp, const app_option_t *opt,
                      app_result_t *r)
{
    uint32_t i;
    uint8_t *buf = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
    if (!buf) {
        return -1;
    }
    uint64_t t0 = app_time_us();
    for (i = 0; i < opt->items; i++) {
        char name[32];
        fill_buf(buf, opt->vlen, 1000 + i);
        make_name(name, sizeof(name),
                  comp->category[0] == 'f' ? "f" : "k", i);
        if (!strcmp(comp->category, "kv")) {
            if (comp->kv_set(name, buf, (uint16_t)opt->vlen) != 0) {
                r->lost++;
            }
        } else if (!strcmp(comp->category, "fs")) {
            if (comp->fs_write(name, buf, opt->vlen) != 0) {
                r->lost++;
            }
        } else {
            if (comp->bm_save(buf, opt->vlen) != 0) {
                r->lost++;
            }
        }
        r->ops++;
        r->app_bytes += opt->vlen;
    }
    r->wall_us = app_time_us() - t0;
    free(buf);
    report_result("write", comp, r);
    return 0;
}

/* 任务：读取性能 */
static int task_read(const app_component_t *comp, const app_option_t *opt,
                     app_result_t *r)
{
    uint32_t i;
    uint8_t *buf = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
    if (!buf) {
        return -1;
    }
    /* 先写入数据 */
    for (i = 0; i < opt->items; i++) {
        char name[32];
        fill_buf(buf, opt->vlen, 2000 + i);
        make_name(name, sizeof(name),
                  comp->category[0] == 'f' ? "f" : "k", i);
        if (!strcmp(comp->category, "kv")) {
            comp->kv_set(name, buf, (uint16_t)opt->vlen);
        } else if (!strcmp(comp->category, "fs")) {
            comp->fs_write(name, buf, opt->vlen);
        } else {
            comp->bm_save(buf, opt->vlen);
        }
    }
    uint64_t t0 = app_time_us();
    for (i = 0; i < opt->items; i++) {
        char name[32];
        make_name(name, sizeof(name),
                  comp->category[0] == 'f' ? "f" : "k", i);
        if (!strcmp(comp->category, "kv")) {
            uint16_t rl = (uint16_t)opt->vlen;
            if (comp->kv_get(name, buf, &rl) == 0) {
                r->ops++;
            } else {
                r->lost++;
            }
        } else if (!strcmp(comp->category, "fs")) {
            uint32_t rl = opt->vlen;
            if (comp->fs_read(name, buf, &rl) == 0) {
                r->ops++;
            } else {
                r->lost++;
            }
        } else {
            if (comp->bm_load(buf, opt->vlen) == 0) {
                r->ops++;
            } else {
                r->lost++;
            }
        }
        r->app_bytes += opt->vlen;
    }
    r->wall_us = app_time_us() - t0;
    free(buf);
    report_result("read", comp, r);
    return 0;
}

/* 任务：更新覆盖 */
static int task_update(const app_component_t *comp, const app_option_t *opt,
                       app_result_t *r)
{
    uint32_t i, rnd_round;
    uint8_t *buf = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
    if (!buf) {
        return -1;
    }
    uint64_t t0 = app_time_us();
    for (rnd_round = 0; rnd_round < opt->rounds; rnd_round++) {
        for (i = 0; i < opt->items; i++) {
            char name[32];
            fill_buf(buf, opt->vlen, 3000 + rnd_round * 1000 + i);
            make_name(name, sizeof(name),
                      comp->category[0] == 'f' ? "f" : "k", i);
            if (!strcmp(comp->category, "kv")) {
                if (comp->kv_set(name, buf, (uint16_t)opt->vlen) == 0) {
                    kv_verify(comp, name, buf, (uint16_t)opt->vlen, r);
                } else {
                    r->lost++;
                }
            } else if (!strcmp(comp->category, "fs")) {
                if (comp->fs_write(name, buf, opt->vlen) == 0) {
                    fs_verify(comp, name, buf, opt->vlen, r);
                } else {
                    r->lost++;
                }
            } else {
                if (comp->bm_save(buf, opt->vlen) == 0) {
                    uint8_t *rb = (uint8_t *)malloc(opt->vlen);
                    if (!rb) {
                        r->lost++;
                    } else if (comp->bm_load(rb, opt->vlen) != 0 ||
                               memcmp(rb, buf, opt->vlen) != 0) {
                        r->lost++;
                    }
                    free(rb);
                } else {
                    r->lost++;
                }
            }
            r->ops++;
            r->app_bytes += opt->vlen;
        }
    }
    r->wall_us = app_time_us() - t0;
    free(buf);
    report_result("update", comp, r);
    return 0;
}

/* 任务：耐久性（高频写入 + 读回校验，关注磨损/写放大） */
static int task_durability(const app_component_t *comp, const app_option_t *opt,
                           app_result_t *r)
{
    uint32_t i, rnd_round;
    uint8_t *buf = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
    if (!buf) {
        return -1;
    }
    uint64_t t0 = app_time_us();
    for (rnd_round = 0; rnd_round < opt->rounds; rnd_round++) {
        for (i = 0; i < opt->items; i++) {
            char name[32];
            fill_buf(buf, opt->vlen, 4000 + rnd_round * 1000 + i);
            make_name(name, sizeof(name),
                      comp->category[0] == 'f' ? "f" : "k", i);
            if (!strcmp(comp->category, "kv")) {
                if (comp->kv_set(name, buf, (uint16_t)opt->vlen) == 0) {
                    kv_verify(comp, name, buf, (uint16_t)opt->vlen, r);
                } else {
                    r->lost++;
                }
            } else if (!strcmp(comp->category, "fs")) {
                if (comp->fs_write(name, buf, opt->vlen) == 0) {
                    fs_verify(comp, name, buf, opt->vlen, r);
                } else {
                    r->lost++;
                }
            } else {
                if (comp->bm_save(buf, opt->vlen) == 0) {
                    r->ops++;
                } else {
                    r->lost++;
                }
            }
            r->ops++;
            r->app_bytes += opt->vlen;
        }
    }
    r->wall_us = app_time_us() - t0;
    free(buf);
    report_result("durability", comp, r);
    return 0;
}

/* 任务：掉电安全（写后重新初始化，验证数据仍在） */
static int task_powerloss(const app_component_t *comp, const app_option_t *opt,
                          app_result_t *r)
{
    uint32_t i;
    uint8_t *buf = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
    if (!buf) {
        return -1;
    }
    /* 写入 N 条数据 */
    for (i = 0; i < opt->items; i++) {
        char name[32];
        fill_buf(buf, opt->vlen, 5000 + i);
        make_name(name, sizeof(name),
                  comp->category[0] == 'f' ? "f" : "k", i);
        if (!strcmp(comp->category, "kv")) {
            if (comp->kv_set(name, buf, (uint16_t)opt->vlen) == 0) {
                r->ops++;
            } else {
                r->lost++;
            }
        } else if (!strcmp(comp->category, "fs")) {
            if (comp->fs_write(name, buf, opt->vlen) == 0) {
                r->ops++;
            } else {
                r->lost++;
            }
        } else {
            if (comp->bm_save(buf, opt->vlen) == 0) {
                r->ops++;
            } else {
                r->lost++;
            }
        }
        r->app_bytes += opt->vlen;
    }

    /* 模拟掉电重启：重新初始化组件（底层介质保持/重新加载）。
     * APP_REINIT=1 通知适配器跳过擦除/格式化/删文件，仅重新挂载。 */
    printf("  [info] 模拟掉电，重新初始化组件…\n");
    uint64_t t0 = app_time_us();
    setenv("APP_REINIT", "1", 1);
    comp->deinit();
    if (comp->init(opt) != 0) {
        unsetenv("APP_REINIT");
        r->lost += opt->items;
        free(buf);
        return -1;
    }
    unsetenv("APP_REINIT");
    r->wall_us = app_time_us() - t0;

    /* 校验数据仍在。
     * 裸机组件是单配置体（覆盖写），掉电后只保留最后一条，仅校验它。 */
    if (!strcmp(comp->category, "baremetal")) {
        fill_buf(buf, opt->vlen, 5000 + (opt->items > 0 ? opt->items - 1 : 0));
        uint8_t *rb = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
        if (!rb) {
            r->lost++;
        } else if (comp->bm_load(rb, opt->vlen) != 0 ||
                   memcmp(rb, buf, opt->vlen) != 0) {
            r->lost++;
        }
        free(rb);
        r->ops++;
        free(buf);
        report_result("powerloss", comp, r);
        return 0;
    }

    /* KV/FS：逐条校验 */
    for (i = 0; i < opt->items; i++) {
        char name[32];
        fill_buf(buf, opt->vlen, 5000 + i);
        make_name(name, sizeof(name),
                  comp->category[0] == 'f' ? "f" : "k", i);
        if (!strcmp(comp->category, "kv")) {
            kv_verify(comp, name, buf, (uint16_t)opt->vlen, r);
        } else {
            fs_verify(comp, name, buf, opt->vlen, r);
        }
        r->ops++;
    }
    free(buf);
    report_result("powerloss", comp, r);
    return 0;
}

/* 任务：混合读写删 */
static int task_mixed(const app_component_t *comp, const app_option_t *opt,
                      app_result_t *r)
{
    uint32_t i, rnd_round;
    uint8_t *buf = (uint8_t *)malloc(opt->vlen ? opt->vlen : 1);
    if (!buf) {
        return -1;
    }
    uint64_t t0 = app_time_us();
    for (rnd_round = 0; rnd_round < opt->rounds; rnd_round++) {
        for (i = 0; i < opt->items; i++) {
            char name[32];
            fill_buf(buf, opt->vlen, 6000 + rnd_round * 1000 + i);
            make_name(name, sizeof(name),
                      comp->category[0] == 'f' ? "f" : "k", i);
            if (!strcmp(comp->category, "kv")) {
                if (i % 4 == 3) {
                    if (comp->kv_del(name) != 0) {
                        r->lost++;
                    }
                } else {
                    if (comp->kv_set(name, buf, (uint16_t)opt->vlen) != 0) {
                        r->lost++;
                    }
                }
            } else if (!strcmp(comp->category, "fs")) {
                if (i % 4 == 3) {
                    if (comp->fs_delete(name) != 0) {
                        r->lost++;
                    }
                } else {
                    if (comp->fs_write(name, buf, opt->vlen) != 0) {
                        r->lost++;
                    }
                }
            } else {
                if (rnd_round % 2 == 0) {
                    if (comp->bm_save(buf, opt->vlen) != 0) {
                        r->lost++;
                    }
                }
            }
            r->ops++;
            r->app_bytes += opt->vlen;
        }
    }
    r->wall_us = app_time_us() - t0;
    free(buf);
    report_result("mixed", comp, r);
    return 0;
}

/* ---------- 任务分发 ---------- */

int app_run_task(const app_component_t *comp, app_task_id_t task)
{
    if (!comp) {
        printf("  [FAIL] 组件适配器为空\n");
        return -1;
    }

    /* 读取测试选项（应用层测试选项来源）。
     * 默认容量 0 = 由适配器按介质总容量自决（KV 类偏小、FS 类用整片），
     * 避免块式文件系统（每文件至少 1 个擦除块）默认容量不足。 */
    app_option_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.items = app_env_u32("APP_ITEMS", 10);
    opt.vlen = app_env_u32("APP_VLEN", 32);
    opt.rounds = app_env_u32("APP_ROUNDS", 20);
    opt.freq = app_env_u32("APP_FREQ", 50);
    opt.capacity = app_env_u32("APP_CAPACITY", 0);

    printf("=== 应用层测试: 组件=%s(%s) 任务=%s ===\n",
           comp->name, comp->category, app_task_names[task]);
    printf("  [info] 选项: items=%u vlen=%u rounds=%u freq=%u%% cap=%u\n",
           opt.items, opt.vlen, opt.rounds, opt.freq, opt.capacity);

    if (comp->init(&opt) != 0) {
        printf("  [FAIL] 组件初始化失败\n");
        return -1;
    }

    app_result_t res;
    memset(&res, 0, sizeof(res));
    res.erase_cycles = 0;

    int rc = 0;
    switch (task) {
    case APP_TASK_WRITE:
        rc = task_write(comp, &opt, &res);
        break;
    case APP_TASK_READ:
        rc = task_read(comp, &opt, &res);
        break;
    case APP_TASK_UPDATE:
        rc = task_update(comp, &opt, &res);
        break;
    case APP_TASK_DURABILITY:
        rc = task_durability(comp, &opt, &res);
        break;
    case APP_TASK_POWERLOSS:
        rc = task_powerloss(comp, &opt, &res);
        break;
    case APP_TASK_MIXED:
        rc = task_mixed(comp, &opt, &res);
        break;
    default:
        printf("  [FAIL] 未知任务\n");
        rc = -1;
        break;
    }

    comp->deinit();
    printf("\n=== 应用层测试结果: %s ===\n",
           (rc == 0 && res.lost == 0) ? "全部通过" : "存在失败");
    return (rc == 0 && res.lost == 0) ? 0 : 1;
}

int app_run_by_name(const char *comp_id, const char *task_name)
{
    const app_component_t *comp = app_find(comp_id);
    if (!comp) {
        printf("  [FAIL] 未知组件: %s（可用组件见 registry）\n", comp_id);
        return -1;
    }
    app_task_id_t task = app_task_parse(task_name);
    if (task == APP_TASK_COUNT) {
        printf("  [FAIL] 未知任务: %s\n", task_name ? task_name : "(null)");
        return -1;
    }
    return app_run_task(comp, task);
}
