/**
 * app_common.h - 应用层测试框架公共定义
 *
 * 本文件属于"应用层测试框架"（app/）的公共类型与接口定义：
 *   - 测试选项（app_option_t）：由环境变量 APP_* 注入，描述一次测试
 *     的参数（数据项数量/长度/轮数/修改频率/分区容量）。
 *   - 测试任务（app_task_id_t）：应用层面向用户的任务，如写入性能、
 *     读取性能、更新覆盖、耐久磨损、掉电安全、混合读写。
 *   - 结果统计（app_result_t）：应用层独立的性能计算（墙钟耗时、
 *     阻塞耗时、写放大、吞吐、磨损均衡等）。
 *   - 组件适配器（app_component_t）：适配层统一接口。将三类组件
 *     （KV / 文件系统 / 裸机）封装为统一语义，供任务引擎调用。
 *
 * 分层定位：
 *   应用层测试框架(app/) -> 适配层(app_component_t) -> 组件层(frameworks/)
 *   -> 驱动层(simulator/flash_sim)
 */

#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 测试选项 ==================== */

typedef struct {
    uint32_t items;     /* 数据项数量（KV 条目 / 文件数 / 保存次数） */
    uint32_t vlen;      /* 单条数据长度（字节） */
    uint32_t rounds;    /* 更新 / 耐久轮数 */
    uint32_t freq;      /* 每轮修改比例（0~100） */
    uint32_t capacity;  /* 组件区容量（字节） */
} app_option_t;

/* ==================== 测试任务 ==================== */

typedef enum {
    APP_TASK_WRITE = 0,      /* 写入性能压测 */
    APP_TASK_READ,           /* 读取性能压测 */
    APP_TASK_UPDATE,         /* 更新覆盖压测 */
    APP_TASK_DURABILITY,     /* 耐久性 / 磨损压测 */
    APP_TASK_POWERLOSS,      /* 掉电安全 */
    APP_TASK_MIXED,          /* 混合读写 */
    APP_TASK_COUNT
} app_task_id_t;

/* 任务名（与枚举一一对应，供环境变量 APP_TASK 解析） */
extern const char *const app_task_names[APP_TASK_COUNT];

/* 按名称解析任务；未知返回 APP_TASK_COUNT */
app_task_id_t app_task_parse(const char *name);

/* ==================== 应用层结果统计 ==================== */

typedef struct {
    uint32_t ops;          /* 应用层总操作数 */
    uint32_t lost;         /* 数据丢失/校验失败次数 */
    uint64_t wall_us;      /* 墙钟耗时（us，应用层计时） */
    uint64_t block_us;     /* 介质阻塞耗时（读+写+擦，us） */
    uint32_t reads;        /* 介质读次数 */
    uint32_t writes;       /* 介质写次数 */
    uint32_t erases;       /* 介质擦除次数 */
    uint32_t app_bytes;    /* 应用层有效写入字节 */
    uint32_t media_bytes;  /* 介质实际写入字节（含写放大） */
    uint32_t max_cycles;   /* 全片最高擦写次数 */
    uint32_t avg_cycles;   /* 全片平均擦写次数 */
    uint32_t erase_cycles; /* 标称擦写寿命 */
    uint32_t bad_blocks;   /* 坏块数 */
    double   write_amp;    /* 写放大 = media_bytes / app_bytes */
    double   ops_per_sec;  /* 应用层吞吐（ops/s） */
    double   throughput_kbps; /* 有效数据吞吐（KB/s） */
} app_result_t;

/* ==================== 组件适配器接口（适配层） ==================== */

typedef struct app_component app_component_t;

struct app_component {
    const char *id;        /* 组件 id（与 registry 一致） */
    const char *category;  /* "kv" / "fs" / "baremetal" */
    const char *name;      /* 展示名 */

    /* 生命周期（组件实现中负责底层模拟基座初始化） */
    int  (*init)(const app_option_t *opt);   /* 0 成功 */
    void (*deinit)(void);
    flash_dev_t *(*device)(void);            /* 返回底层设备（统计用） */

    /* ---- KV 语义（category=="kv" 时使用） ---- */
    int  (*kv_set)(const char *key, const void *val, uint16_t len);
    int  (*kv_get)(const char *key, void *val, uint16_t *len);
    int  (*kv_del)(const char *key);

    /* ---- FS 语义（category=="fs" 时使用） ---- */
    int  (*fs_write)(const char *name, const void *buf, uint32_t len);
    int  (*fs_read)(const char *name, void *buf, uint32_t *len);
    int  (*fs_append)(const char *name, const void *buf, uint32_t len);
    int  (*fs_delete)(const char *name);
    int  (*fs_get_size)(const char *name, uint32_t *size);

    /* ---- BM 语义（category=="baremetal" 时使用） ---- */
    int  (*bm_save)(const void *data, uint32_t len);
    int  (*bm_load)(void *data, uint32_t len);
};

/* ==================== 公共工具 ==================== */

/* 读取环境变量 uint32（缺省/空/非法返回 def） */
uint32_t app_env_u32(const char *key, uint32_t def);

/* 读取环境变量字符串（缺省/空返回 def） */
const char *app_env_str(const char *key, const char *def);

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H */
