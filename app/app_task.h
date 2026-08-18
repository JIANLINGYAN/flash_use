/**
 * app_task.h - 应用层测试任务引擎
 *
 * 定义应用层测试框架的"任务"概念：
 *   应用层任务 = 一组有明确目标的测试选项（测试用例），通过适配层
 *   （app_component_t）调用组件层框架，并在应用层独立做性能计算。
 *
 * 任务与适配层/组件层的关系：
 *   应用层任务（本文件）-> 适配层（app_component_t）-> 组件层(frameworks)
 *   -> 驱动层(simulator)。
 *
 * 每个任务为一次完整运行：执行目标操作并统计墙钟/介质耗时、操作数、
 * 数据丢失、磨损与写放大，最后输出 STATS_JSON 与 WEARMAP。
 */

#ifndef APP_TASK_H
#define APP_TASK_H

#include "app_common.h"
#include "app_register.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 运行指定组件的一个任务。
 * @param comp     组件适配器（app_find 获取）
 * @param task     任务枚举
 * @return         0 成功；负值失败
 */
int app_run_task(const app_component_t *comp, app_task_id_t task);

/**
 * 运行指定组件 id 的指定任务名（内部解析并查找组件）。
 * @return         0 成功；负值失败（未知组件/任务）
 */
int app_run_by_name(const char *comp_id, const char *task_name);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASK_H */
