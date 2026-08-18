/**
 * app_util.h - 应用层工具（环境变量 / 计时 / 日志）
 *
 * 提供应用层测试框架的公共辅助函数：环境变量读取（与组件层测试程序
 * 约定一致）、单调时钟计时、统一日志与结果输出。
 */

#ifndef APP_UTIL_H
#define APP_UTIL_H

#include <stdint.h>

#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 环境变量 ---------- */

/* 读取 uint32 环境变量；缺省/空/非法返回 def */
uint32_t app_env_u32(const char *key, uint32_t def);

/* 读取字符串环境变量；缺省/空返回 def */
const char *app_env_str(const char *key, const char *def);

/* 判断逗号分隔的环境变量列表是否包含 name（空/缺省视为包含全部） */
int app_env_has(const char *key, const char *name);

/* 从 SIM_* 环境变量构造模拟基座配置（介质类型/容量/块大小/寿命/耗时/坏块） */
void app_sim_make_config(flash_config_t *cfg, const char *bin_path,
                         flash_type_t def_type);

/* ---------- 计时 ---------- */

/* 返回单调时钟当前值（us） */
uint64_t app_time_us(void);

/* ---------- 日志与结果 ---------- */

/* 打印结果 STATS_JSON（后端 parse_output 解析） */
void app_print_stats(const char *mode, const app_result_t *r);

/* 打印整片磨损图 WEARMAP（后端渲染热力图） */
void app_print_wearmap(flash_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* APP_UTIL_H */
