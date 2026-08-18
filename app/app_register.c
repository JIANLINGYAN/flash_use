/**
 * app_register.c - 组件适配器注册表实现
 *
 * 维护已注册组件适配器的数组。各适配器文件通过构造函数
 * （__attribute__((constructor))）在 main 之前调用 app_register()。
 */

#include "app_register.h"

#include <string.h>

#define APP_REGISTER_MAX 32

static const app_component_t *s_list[APP_REGISTER_MAX];
static unsigned s_count = 0;

void app_register(const app_component_t *comp)
{
    if (!comp || !comp->id) {
        return;
    }
    unsigned i;
    for (i = 0; i < s_count; i++) {
        if (s_list[i] == comp) {
            return; /* 已注册，忽略 */
        }
    }
    if (s_count < APP_REGISTER_MAX) {
        s_list[s_count++] = comp;
    }
}

const app_component_t *app_find(const char *id)
{
    if (!id) {
        return NULL;
    }
    unsigned i;
    for (i = 0; i < s_count; i++) {
        if (s_list[i]->id && strcmp(s_list[i]->id, id) == 0) {
            return s_list[i];
        }
    }
    return NULL;
}

void app_foreach(void (*fn)(const app_component_t *comp, void *arg), void *arg)
{
    unsigned i;
    for (i = 0; i < s_count; i++) {
        if (fn) {
            fn(s_list[i], arg);
        }
    }
}

unsigned app_component_count(void)
{
    return s_count;
}
