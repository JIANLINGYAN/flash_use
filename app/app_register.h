/**
 * app_register.h - 组件适配器注册表
 *
 * 每个组件适配器（app/adapter 目录下的 .c）通过构造函数在 main 前
 * 自注册，由应用层入口（app/test/main_app.c）按 APP_COMPONENT 环境
 * 变量选择。编译时仅链接目标组件源码 + 其适配器，其余适配器不参与
 * 链接，因此未注册的组件自然不可用，符号不冲突。
 */

#ifndef APP_REGISTER_H
#define APP_REGISTER_H

#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 注册一个组件适配器（由适配器构造函数调用） */
void app_register(const app_component_t *comp);

/* 按 id 查找；未找到返回 NULL */
const app_component_t *app_find(const char *id);

/* 遍历所有已注册组件 */
void app_foreach(void (*fn)(const app_component_t *comp, void *arg),
                 void *arg);

/* 已注册组件数量 */
unsigned app_component_count(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_REGISTER_H */
