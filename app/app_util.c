/**
 * app_util.c - 应用层工具实现
 */

/* POSIX 时钟（clock_gettime）需显式开启，兼容 -std=c99 严格模式 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "app_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint32_t app_env_u32(const char *key, uint32_t def)
{
    const char *v = getenv(key);
    if (!v || !*v) {
        return def;
    }
    char *end = NULL;
    unsigned long n = strtoul(v, &end, 0);
    if (end == v) {
        return def;
    }
    return (uint32_t)n;
}

const char *app_env_str(const char *key, const char *def)
{
    const char *v = getenv(key);
    return (v && *v) ? v : def;
}

int app_env_has(const char *key, const char *name)
{
    const char *v = getenv(key);
    if (!v || !*v) {
        return 1; /* 未指定则视为全部启用 */
    }
    const char *p = v;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == strlen(name) && strncmp(p, name, len) == 0) {
            return 1;
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

void app_sim_make_config(flash_config_t *cfg, const char *bin_path,
                         flash_type_t def_type)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->bin_path = bin_path ? bin_path : "app_sim.bin";
    FLASH_CFG_DEFAULTS_BY_TYPE(*cfg, def_type);
    cfg->type = (flash_type_t)app_env_u32("SIM_TYPE", (uint32_t)def_type);
    FLASH_CFG_DEFAULTS_BY_TYPE(*cfg, cfg->type);
    cfg->total_size = app_env_u32("SIM_TOTAL", cfg->total_size);
    cfg->erase_size = app_env_u32("SIM_ERASE", cfg->erase_size);
    cfg->write_size = app_env_u32("SIM_WRITE", cfg->write_size);
    cfg->read_size = 1;
    cfg->erase_cycles = app_env_u32("SIM_CYCLES", cfg->erase_cycles);
    cfg->read_us = app_env_u32("SIM_RD_US", cfg->read_us);
    cfg->write_us = app_env_u32("SIM_WR_US", cfg->write_us);
    cfg->erase_us = app_env_u32("SIM_ERASE_US", cfg->erase_us);
    cfg->bad_blocks = app_env_u32("SIM_BAD_N", cfg->bad_blocks);
    cfg->bad_ratio = app_env_u32("SIM_BAD_R", cfg->bad_ratio);
}

uint64_t app_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
}

void app_print_stats(const char *mode, const app_result_t *r)
{
    if (!r) {
        return;
    }
    /* 应用层独立性能计算：吞吐与写放大 */
    double ops_ps = 0.0, kbps = 0.0, wamp = 0.0;
    if (r->wall_us > 0) {
        ops_ps = (double)r->ops * 1000000.0 / (double)r->wall_us;
        kbps = (double)r->app_bytes / 1024.0 * 1000000.0 / (double)r->wall_us;
    }
    if (r->app_bytes > 0) {
        wamp = (double)r->media_bytes / (double)r->app_bytes;
    }
    printf("STATS_JSON:{\"mode\":\"%s\",\"ops\":%u,\"lost\":%u,"
           "\"wall_us\":%llu,\"block_us\":%llu,"
           "\"reads\":%u,\"writes\":%u,\"erases\":%u,"
           "\"app_bytes\":%u,\"media_bytes\":%u,"
           "\"write_amp\":%.2f,\"ops_per_sec\":%.1f,\"kbps\":%.1f,"
           "\"max_cycles\":%u,\"avg_cycles\":%u,\"erase_cycles\":%u,"
           "\"bad_blocks\":%u}\n",
           mode, r->ops, r->lost,
           (unsigned long long)r->wall_us,
           (unsigned long long)r->block_us,
           r->reads, r->writes, r->erases,
           r->app_bytes, r->media_bytes,
           wamp, ops_ps, kbps,
           r->max_cycles, r->avg_cycles, r->erase_cycles,
           r->bad_blocks);
}

void app_print_wearmap(flash_dev_t *dev)
{
    if (!dev) {
        return;
    }
    uint32_t bc = flash_sim_block_count(dev);
    if (bc == 0) {
        return;
    }
    uint32_t *map = (uint32_t *)malloc(sizeof(uint32_t) * bc);
    if (!map) {
        return;
    }
    uint32_t got = flash_sim_get_wear_map(dev, map, bc);
    printf("WEARMAP:");
    uint32_t i;
    for (i = 0; i < got; i++) {
        printf("%s%u", i ? "," : "", map[i]);
    }
    printf("\n");
    free(map);
}
