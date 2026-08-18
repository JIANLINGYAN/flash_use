/**
 * main_tym_setting.c - TYM Setting 组件在模拟基座上的运行验证
 *
 * 定位：TYM Setting 是"ID 索引静态表 + RAM 全镜像 + 延时批量整页回写"
 * 的极简持久化框架（无磨损均衡/无 GC，掉电安全性弱，见 PORTING.md）。
 * 按"基本可用"标准验证：写入 → 回写 → 重载 → 读取 → 修改 → 回写 → 再读。
 *
 * 测试驱动（环境变量）：
 *   SIM_*        模拟基座参数
 *   KV_CAPACITY  Setting 区容量(字节)，须为擦除块整数倍且 >= 2 块
 *   KV_TESTS     basic / reinit / modify（默认全跑）
 *
 * 输出：STATS_JSON:{...} 与 WEARMAP:...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_setting_idle_activity.h"
#include "SettingSrv_priv.h"

#include "flash_hal_adapter.h"
#include "flash_sim.h"
#include "tym_setting_sim_port.h"

#define TYM_BIN "tym_setting_demo.bin"

static int g_fail = 0;
static flash_dev_t *s_dev;

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

static int has_test(const char *tests, const char *name)
{
    if (!tests || !*tests) { return 1; }
    const char *p = tests;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == strlen(name) && strncmp(p, name, len) == 0) { return 1; }
        if (!comma) { break; }
        p = comma + 1;
    }
    return 0;
}

/* 安全读取：返回 NULL 时置 *ok=0 */
static uint32_t safe_get_u32(eSettingId id, int *ok)
{
    const void *p = Setting_Get(id);

    if (p == NULL) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return *(const uint32_t *)p;
}

static const char *safe_get_str(eSettingId id)
{
    return (const char *)Setting_Get(id);
}

/* 首次格式化 + 初始化（模拟首次上电） */
static void tym_format_and_init(void)
{
    tym_setting_sim_erase_all();
    SettingSrv_Init();
}

/* 重新初始化（模拟设备重启，重新从 Flash 载入） */
static void tym_reinit(void)
{
    SettingSrv_Init();
}

/* ---------------- 基本写读改 ---------------- */

static void t_basic(void)
{
    uint32_t vol = 60;

    /* 清除载入产生的 SET 位，验证"未写前不就绪" */
    SettingSrv_InitDB();
    expect("basic: 初始未就绪", Setting_IsReady(SETID_MASTER_VOL) == FALSE);

    /* 写入 */
    Setting_Set(SETID_MASTER_VOL, &vol);
    expect("basic: 写入后 RAM 就绪", Setting_IsReady(SETID_MASTER_VOL) == TRUE);
    expect("basic: 读回一致",
           *(const uint32_t *)Setting_Get(SETID_MASTER_VOL) == vol);

    /* 修改 */
    vol = 75;
    Setting_Set(SETID_MASTER_VOL, &vol);
    expect("basic: 修改后读回一致",
           *(const uint32_t *)Setting_Get(SETID_MASTER_VOL) == vol);

    /* 非 NVM 项：写 RAM 但不应触发回写（无 Flash 地址） */
    int32_t lvl = -12;
    Setting_Set(SETID_CH1_SIG_LEVEL, &lvl);
    expect("basic: 非 NVM 项可写",
           *(const int32_t *)Setting_Get(SETID_CH1_SIG_LEVEL) == lvl);
    expect("basic: 非 NVM 项大小", Setting_GetSize(SETID_CH1_SIG_LEVEL) == 4);

    /* <4 字节项 */
    uint8_t fr = 1;
    Setting_Set(SETID_FACTORY_RESET, &fr);
    expect("basic: 小尺寸项写入",
           *(const uint8_t *)Setting_Get(SETID_FACTORY_RESET) == fr);

    /* 越界防护 */
    uint32_t dummy = 0;
    Setting_Set(SETID_MAX, &dummy);   /* 应被拒绝不崩溃 */
    expect("basic: 越界 ID 被拒绝",
           Setting_GetSize(SETID_MAX) == 0 && Setting_IsIdValid(SETID_MAX) == FALSE);
}

/* 延时回写 + 重载持久化（核心：写入 → tick 到点 → 重启 → 读取） */
static void t_persist(void)
{
    uint32_t vol = 80;
    char name[24];

    memset(name, 0, sizeof(name));
    strcpy(name, "demo-device");

    /* 先写入两笔，再手动走完保存窗口 */
    Setting_Set(SETID_MASTER_VOL, &vol);
    Setting_Set(SETID_BT_NAME, name);

    /* 模拟裸机 tick：<5s 时不回写 */
    SettingSrv_Tick(1000);
    SettingSrv_Tick(1000);

    /* 模拟设备重启：重新初始化（Flash 尚未回写 → 读到默认/旧值） */
    tym_reinit();
    int ok = 0;
    uint32_t rv = safe_get_u32(SETID_MASTER_VOL, &ok);
    int rv_ok = (!ok || rv != vol);   /* 未回写则不应读到新值 */
    printf("    [info] 未回写重启读到 vol=%u ok=%d（期望 != %u）\n",
           (unsigned)rv, ok, (unsigned)vol);
    expect("persist: 5s 内断电不保存", rv_ok);

    /* 重新写入并走完窗口（重载后 RAM 被 Flash 旧值覆盖，需重新 Set） */
    Setting_Set(SETID_MASTER_VOL, &vol);
    Setting_Set(SETID_BT_NAME, name);
    SettingSrv_Tick(5000);   /* 到点触发整页回写 */

    /* 重启重载：应读到回写值 */
    tym_reinit();
    rv = safe_get_u32(SETID_MASTER_VOL, &ok);
    printf("    [info] 回写后重启读到 vol=%u\n", (unsigned)rv);
    expect("persist: 回写后重启保留", ok && rv == vol);
    expect("persist: BT_NAME 也保留",
           safe_get_str(SETID_BT_NAME) != NULL &&
           strcmp(safe_get_str(SETID_BT_NAME), name) == 0);
}

/* 修改并再次回写（覆盖持久化） */
static void t_modify_persist(void)
{
    uint32_t vol = 95;
    int ok = 0;

    Setting_Set(SETID_MASTER_VOL, &vol);
    SettingSrv_Tick(5000);
    tym_reinit();

    uint32_t rv = safe_get_u32(SETID_MASTER_VOL, &ok);
    expect("modify: 修改后重启保留", ok && rv == vol);
}

int main(void)
{
    printf("=== TYM Setting 组件运行验证 ===\n");

    flash_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = (flash_type_t)env_long("SIM_TYPE", FLASH_TYPE_NOR);
    cfg.total_size = (uint32_t)env_long("SIM_TOTAL", 64 * 1024);
    cfg.erase_size = (uint32_t)env_long("SIM_ERASE", 4 * 1024);
    cfg.write_size = (uint32_t)env_long("SIM_WRITE", 1);
    cfg.read_size = 1;
    cfg.erase_cycles = (uint32_t)env_long("SIM_CYCLES", 100000);
    cfg.bin_path = TYM_BIN;
    cfg.read_us = (uint32_t)env_long("SIM_RD_US", 0);
    cfg.write_us = (uint32_t)env_long("SIM_WR_US", 0);
    cfg.erase_us = (uint32_t)env_long("SIM_ERASE_US", 0);
    cfg.bad_blocks = (uint32_t)env_long("SIM_BAD_N", 0);
    cfg.bad_ratio = (uint32_t)env_long("SIM_BAD_R", 0);

    if (cfg.type == FLASH_TYPE_EEPROM) {
        printf("  [info] TYM Setting 依赖块擦除语义，EEPROM 自动切换为 NOR\n");
        cfg.type = FLASH_TYPE_NOR;
    }

    s_dev = flash_sim_init(&cfg);
    if (!s_dev) {
        printf("  Flash 初始化失败!\n");
        return 1;
    }

    uint32_t capacity = (uint32_t)env_long("KV_CAPACITY",
                            (cfg.total_size >= 16384) ? 16384 : cfg.total_size);
    capacity -= capacity % cfg.erase_size;
    if (capacity < cfg.erase_size * 2u) { capacity = cfg.erase_size * 2u; }
    if (capacity > cfg.total_size) {
        capacity = cfg.total_size - (cfg.total_size % cfg.erase_size);
    }
    printf("  [info] 介质=%s 容量=%uB 块=%uB | Setting 区=%uB (%u 块)\n",
           cfg.type == FLASH_TYPE_NAND ? "NAND" : "NOR",
           cfg.total_size, cfg.erase_size, capacity, capacity / cfg.erase_size);

    /* 打开介质 -> 包装为统一 flash_hal_t -> 注册给移植层 */
    flash_hal_t hal;
    flash_hal_from_sim(s_dev, cfg.total_size, cfg.erase_size, cfg.write_size, &hal);
    tym_setting_sim_setup(&hal, 0, capacity, cfg.erase_size);

    const char *tests = getenv("KV_TESTS");

    if (has_test(tests, "basic")) {
        printf("\n[测试项] 基本写入/读取/修改\n");
        tym_format_and_init();
        t_basic();
    }
    if (has_test(tests, "persist")) {
        printf("\n[测试项] 延时回写与重启持久化\n");
        tym_format_and_init();
        t_persist();
    }
    if (has_test(tests, "modify")) {
        printf("\n[测试项] 修改后再次持久化\n");
        t_modify_persist();
    }

    flash_stats_t st;
    flash_sim_get_stats(s_dev, &st);
    printf("STATS_JSON:{\"mode\":\"basic\",\"reads\":%u,\"writes\":%u,"
           "\"erases\":%u,\"write_bytes\":%u,\"max_cycles\":%u,"
           "\"avg_cycles\":%u,\"read_us\":%llu,\"write_us\":%llu,"
           "\"erase_us\":%llu,\"bad_blocks\":%u,\"erase_cycles\":%u}\n",
           st.total_reads, st.total_writes, st.total_erases,
           st.total_write_bytes, st.max_erase_cycles, st.avg_erase_cycles,
           (unsigned long long)st.read_time_us,
           (unsigned long long)st.write_time_us,
           (unsigned long long)st.erase_time_us, st.bad_block_count,
           cfg.erase_cycles);

    flash_sim_deinit(s_dev);
    printf("\n=== TYM Setting 运行验证结果: %s ===\n",
           g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
