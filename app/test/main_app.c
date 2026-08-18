/**
 * main_app.c - 应用层测试框架统一入口
 *
 * 使用方法（环境变量）：
 *   APP_COMPONENT  组件 id（对应编译进本程序的适配器，如 kv/easyflash/
 *                  littlefs/...）。缺省为 list 时打印已注册组件。
 *   APP_TASK       任务名：write / read / update / durability /
 *                  powerloss / mixed（缺省 durability）
 *   APP_ITEMS      数据项数量（默认 50）
 *   APP_VLEN       单条数据长度（默认 32）
 *   APP_ROUNDS     轮数（默认 20）
 *   APP_FREQ       修改频率%（默认 50）
 *   APP_CAPACITY   组件区容量（默认 8192）
 *   SIM_*          模拟基座参数（介质类型/容量/块大小/寿命/耗时/坏块）
 *
 * 编译时通过链接"目标组件的适配器 + 组件源码 + 模拟基座"得到一个
 * 单组件可执行程序；构造函数会把该适配器注册进注册表。
 *
 * 输出（后端解析）：STATS_JSON:{...} 与 WEARMAP:...；级别前缀与
 * parse_output / _classify_line 约定一致：[INFO] [OK] [FAIL] [WARN]。
 */

#include <stdio.h>
#include <string.h>

#include "app_register.h"
#include "app_task.h"
#include "app_util.h"

static void list_components(const app_component_t *comp, void *arg)
{
    (void)arg;
    printf("  [info] 可用组件: %-12s (%s) %s\n",
           comp->id, comp->category, comp->name);
}

int main(void)
{
    const char *comp_id = app_env_str("APP_COMPONENT", "list");
    const char *task_name = app_env_str("APP_TASK", "durability");

    if (strcmp(comp_id, "list") == 0) {
        printf("=== 已注册组件适配器 ===\n");
        app_foreach(list_components, NULL);
        printf("共 %u 个\n", app_component_count());
        return 0;
    }

    int rc = app_run_by_name(comp_id, task_name);
    return rc == 0 ? 0 : 1;
}
