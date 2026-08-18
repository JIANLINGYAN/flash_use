#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
backend/registry.py - 框架注册表（模块三：框架元数据集中管理）

职责：集中描述本平台所有可测试框架的元信息（展示名、源码组成、编译
include 路径、测试工作目录、可配置项 schema、测试项清单），供
server.py 的 /api/frameworks 与编译运行逻辑消费。

新增框架步骤：
  1. 在 frameworks/<name>/ 下放置框架源码与 test/main_*.c；
  2. 在本文件 FRAMEWORKS 末尾追加一条注册记录；
  3. （可选）在 generator.py 的 RECIPES 追加配方以支持导出。

本文件只包含数据与纯查询函数，不含任何 HTTP / 编译 / IO 逻辑。
"""

# ---------------------------------------------------------------------------
# 模拟基座可配置项 -> 环境变量名（所有框架共用，注入到测试程序）
# ---------------------------------------------------------------------------
SIM_ENV_MAP = {
    "type": "SIM_TYPE",
    "total": "SIM_TOTAL",
    "erase_size": "SIM_ERASE",
    "write_size": "SIM_WRITE",
    "read_us": "SIM_RD_US",
    "write_us": "SIM_WR_US",
    "erase_us": "SIM_ERASE_US",
    "erase_cycles": "SIM_CYCLES",
    "bad_blocks": "SIM_BAD_N",
    "bad_ratio": "SIM_BAD_R",
}
# 视为"未配置、跳过注入"的值。仅作用于数值字段，避免把 schema 中的
# 0 占位默认（"按类型默认"）注入环境变量后被 C 程序当成合法值覆盖兜底。
# type 字段独立于本集合，始终注入。
_SIM_SKIP_IF_ZERO = {
    "total", "erase_size", "write_size", "erase_cycles",
    "read_us", "write_us", "erase_us",
    "bad_blocks", "bad_ratio",
}
# KV 测试可配置项 -> 环境变量名
KV_ENV_MAP = {
    "capacity": "KV_CAPACITY",
    "rounds": "KV_ROUNDS",
    "tests": "KV_TESTS",   # 数组 -> 逗号拼接
    "items": "KV_ITEMS",   # 数组 -> LEN,N,FREQ;... 拼接
}

# ---------------------------------------------------------------------------
# 配置项 schema（前端渲染表单用）：group 区分"模拟基座"与"测试"
# 数值字段的 default 是该字段在"默认类型(NOR)"下的硬件合理值，
# 便于用户开箱即用。type 切换时由前端按 SIM_TYPE_DEFAULTS 刷新
# 其他字段的当前值。
# 后端注入时，对 0 值视为"按类型默认"（不注入到 C 程序环境），
# 让 C 程序走自身兜底或模拟器 FLASH_CFG_DEFAULTS_BY_TYPE。
# ---------------------------------------------------------------------------
SIM_TYPE_DEFAULTS = {
    # key            NOR                      NAND                       EEPROM
    "total":         (64 * 1024,             128 * 1024 * 1024,           32 * 1024),
    "erase_size":    (4 * 1024,              128 * 1024,                  0),         # EEPROM 无擦除
    "write_size":    (1,                     1,                            1),
    "erase_cycles":  (100000,                100000,                       1000000),
    "read_us":       (50,                    100,                          50),
    "write_us":      (200,                   500,                          500),
    "erase_us":      (40000,                 3000,                         0),         # EEPROM 无擦除
    "bad_blocks":    (0,                     0,                            0),
    "bad_ratio":     (0,                     0,                            0),
}

SIM_CONFIG_SCHEMA = [
    {"key": "type", "label": "存储介质类型", "type": "select",
     "options": [["0", "NOR"], ["1", "NAND"], ["2", "EEPROM"]], "default": "0",
     "group": "simulator"},
    {"key": "total", "label": "总容量(字节)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["total"][0], "min": 0, "step": 1024,
     "group": "simulator"},
    {"key": "erase_size", "label": "擦除块大小(字节)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["erase_size"][0], "min": 0, "step": 256,
     "group": "simulator"},
    {"key": "write_size", "label": "最小写入单位(字节)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["write_size"][0], "min": 1, "step": 1,
     "group": "simulator"},
    {"key": "erase_cycles", "label": "标称擦写寿命(次)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["erase_cycles"][0], "min": 0, "step": 1000,
     "group": "simulator"},
    {"key": "read_us", "label": "读耗时(us/次)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["read_us"][0], "min": 0, "step": 1,
     "group": "simulator"},
    {"key": "write_us", "label": "写耗时(us/次)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["write_us"][0], "min": 0, "step": 1,
     "group": "simulator"},
    {"key": "erase_us", "label": "擦除耗时(us/次)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["erase_us"][0], "min": 0, "step": 1,
     "group": "simulator"},
    {"key": "bad_blocks", "label": "固定坏块数量", "type": "number",
     "default": SIM_TYPE_DEFAULTS["bad_blocks"][0], "min": 0, "step": 1,
     "group": "simulator"},
    {"key": "bad_ratio", "label": "运行时坏块比率(万分之)", "type": "number",
     "default": SIM_TYPE_DEFAULTS["bad_ratio"][0], "min": 0, "max": 10000,
     "step": 1, "group": "simulator"},
]


# ---------------------------------------------------------------------------
# 框架分类（前端分组展示 + 应用层测试路由）
#   simulator   驱动层：模拟基座
#   baremetal   组件层：裸机简单框架
#   kv          组件层：KV 管理框架
#   fs          组件层：文件系统框架
# ---------------------------------------------------------------------------
CATEGORY_SIMULATOR = "simulator"
CATEGORY_BAREMETAL = "baremetal"
CATEGORY_KV = "kv"
CATEGORY_FS = "fs"

# 分类展示名（前端分组标题）
CATEGORY_LABELS = {
    CATEGORY_SIMULATOR: "驱动层 · 模拟基座",
    CATEGORY_BAREMETAL: "组件层 · 裸机简单管理",
    CATEGORY_KV: "组件层 · KV 管理",
    CATEGORY_FS: "组件层 · 文件系统",
}

# ---------------------------------------------------------------------------
# 应用层测试框架（app/）：统一任务引擎通过适配层调用组件层。
# APP_CORE_SOURCES 为应用层公共源码（与具体组件无关），编译任一组件时
# 均需链接；APP_ADAPTER_MAP 描述"组件 id -> 适配层源文件"。
# ---------------------------------------------------------------------------
APP_CORE_SOURCES = [
    "app/app_register.c",
    "app/app_util.c",
    "app/app_task.c",
    "app/test/main_app.c",
]
APP_CORE_INCLUDES = ["app", "simulator"]

APP_ADAPTER_MAP = {
    "kv": "app/adapter/kv_store_ad.c",
    "easyflash": "app/adapter/easyflash_ad.c",
    "flashdb": "app/adapter/flashdb_ad.c",
    "fastflash": "app/adapter/fastflash_ad.c",
    "nvdm": "app/adapter/nvdm_ad.c",
    "nvs": "app/adapter/nvs_ad.c",
    "zms": "app/adapter/zms_ad.c",
    "fcb": "app/adapter/fcb_ad.c",
    "tym_setting": "app/adapter/tym_setting_ad.c",
    "baremetal": "app/adapter/baremetal_ad.c",
    "fs": "app/adapter/fs_store_ad.c",
    "littlefs": "app/adapter/littlefs_ad.c",
    "fatfs": "app/adapter/fatfs_ad.c",
    "spiffs": "app/adapter/spiffs_ad.c",
    "yaffs": "app/adapter/yaffs_ad.c",
}

# 应用层测试任务 schema（前端"应用层测试"表单）
APP_TASK_SCHEMA = [
    {"key": "task", "label": "测试任务", "type": "select",
     "options": [
         ["write", "写入性能"],
         ["read", "读取性能"],
         ["update", "更新覆盖"],
         ["durability", "耐久性/磨损"],
         ["powerloss", "掉电安全"],
         ["mixed", "混合读写"],
     ], "default": "durability", "group": "app"},
    {"key": "items", "label": "数据项数量", "type": "number",
     "default": 50, "min": 1, "step": 1, "group": "app"},
    {"key": "vlen", "label": "单条数据长度(字节)", "type": "number",
     "default": 32, "min": 1, "step": 1, "group": "app"},
    {"key": "rounds", "label": "轮数", "type": "number",
     "default": 20, "min": 1, "step": 1, "group": "app"},
    {"key": "freq", "label": "修改频率(%)", "type": "number",
     "default": 50, "min": 0, "max": 100, "step": 1, "group": "app"},
    {"key": "capacity", "label": "组件区容量(字节)", "type": "number",
     "default": 8192, "min": 1024, "step": 1024, "group": "app"},
]


def app_layer_for(fid):
    """返回组件 id 的应用层测试配置：{sources, includes, cflags, adapter}。

    由框架注册项推导：组件源码 + 适配层 + 应用层核心 + 应用层入口，
    includes 合并组件头目录与应用层目录。
    """
    fw = get_framework(fid)
    if not fw:
        return None
    adapter = APP_ADAPTER_MAP.get(fid)
    if not adapter:
        return None
    # 组件源码：注册项 sources 中剔除测试入口（test/ 路径）
    comp_sources = [s for s in fw["sources"]
                    if "/test/" not in s and s != "simulator/flash_sim.c"]
    sources = (["simulator/flash_sim.c"]
               + APP_CORE_SOURCES + [adapter] + comp_sources)
    includes = list(APP_CORE_INCLUDES) + list(fw.get("includes", []))
    return {
        "sources": sources,
        "includes": includes,
        "cflags": list(fw.get("cflags", [])),
        "adapter": adapter,
    }


def app_supported(fid):
    """组件是否支持应用层测试（存在适配器）。"""
    return fid in APP_ADAPTER_MAP


# ---------------------------------------------------------------------------
# 框架注册表：描述每个可测试框架的源码组成与编译方式。
# 每个框架：
#   id          唯一标识
#   name        展示名
#   category    分类（simulator/baremetal/kv/fs）
#   desc        简介
#   sources     参与编译的 C 源文件（相对项目根）
#   includes    编译 -I 包含目录（相对项目根）
#   workdir     测试运行时的工作目录（用于放置 BIN，相对项目根）
#   config_schema / test_schema / test_items  前端表单与测试项
# ---------------------------------------------------------------------------
FRAMEWORKS = [
    {
        "id": "simulator",
        "name": "模拟基座 (NOR/NAND/EEPROM)",
        "category": CATEGORY_SIMULATOR,
        "desc": "Flash 物理特性模拟：按块擦除、写入仅允许 1->0、EEPROM 字节写、"
                "寿命统计、坏块模拟。可配置类型/容量/块大小/速度/坏点。",
        "sources": ["simulator/flash_sim.c", "simulator/test/main_sim.c"],
        "includes": ["simulator"],
        "workdir": "simulator/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [],
        "test_env": {"tests": "SIM_TESTS"},
        "test_items": [
            {"id": "basic", "label": "基础擦除/写/读语义"},
            {"id": "badblock", "label": "坏块拒绝(NAND)"},
            {"id": "wear", "label": "磨损统计/磨损均衡"},
            {"id": "powerloss", "label": "掉电重放持久化"},
        ],
    },
    {
        "id": "kv",
        "name": "KV/NVS 存储框架",
        "category": CATEGORY_KV,
        "desc": "基于模拟基座的键值存储：两步提交掉电安全、CRC 校验、后写覆盖、"
                "删除、压实垃圾回收。支持功能压测（条目数/长度/修改频率）。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/kv/kv_store.c",
            "frameworks/kv/test/main_kv.c",
        ],
        "includes": ["simulator", "frameworks/kv"],
        "workdir": "frameworks/kv/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "KV 区容量(字节)", "type": "number",
             "default": 8192, "min": 1024, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "update", "label": "更新覆盖"},
            {"id": "delete", "label": "删除"},
            {"id": "powerloss", "label": "掉电残留丢弃"},
            {"id": "gc", "label": "压实 GC"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    {
        "id": "easyflash",
        "name": "EasyFlash (ENV/KV 开源组件)",
        "category": CATEGORY_KV,
        "desc": "成熟开源 KV 框架（armink/EasyFlash V4.x NG 模式）：内置磨损均衡、"
                "掉电保护（状态表多阶段提交）与垃圾回收。通过 ef_port 适配本平台"
                "模拟基座，可独立导出使用。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/easyflash/ef_port.c",
            "frameworks/easyflash/vendor/src/ef_env.c",
            "frameworks/easyflash/vendor/src/ef_utils.c",
            "frameworks/easyflash/vendor/src/easyflash.c",
            "frameworks/easyflash/test/main_easyflash.c",
        ],
        "includes": [
            "simulator",
            "frameworks/easyflash",
            "frameworks/easyflash/vendor/inc",
        ],
        "workdir": "frameworks/easyflash/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "ENV 区容量(字节)", "type": "number",
             "default": 8192, "min": 2048, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "update", "label": "更新覆盖"},
            {"id": "delete", "label": "删除"},
            {"id": "powerloss", "label": "掉电安全"},
            {"id": "gc", "label": "垃圾回收"},
            {"id": "types", "label": "多类型数据"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    {
        "id": "flashdb",
        "name": "FlashDB (KVDB 开源组件)",
        "category": CATEGORY_KV,
        "desc": "EasyFlash 作者的下一代作品（armink/FlashDB），KVDB 同样具备磨损均衡、"
                "掉电保护与垃圾回收，并支持 blob 接口。通过 FAL 移植层对接模拟基座。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/flashdb/fal_flash_sim_port.c",
            "frameworks/flashdb/vendor/src/fdb.c",
            "frameworks/flashdb/vendor/src/fdb_utils.c",
            "frameworks/flashdb/vendor/src/fdb_kvdb.c",
            "frameworks/flashdb/vendor/src/fdb_tsdb.c",
            "frameworks/flashdb/vendor/src/fdb_file.c",
            "frameworks/flashdb/vendor/fal/src/fal.c",
            "frameworks/flashdb/vendor/fal/src/fal_flash.c",
            "frameworks/flashdb/vendor/fal/src/fal_partition.c",
            "frameworks/flashdb/test/main_flashdb.c",
        ],
        "includes": [
            "simulator",
            "frameworks/flashdb",
            "frameworks/flashdb/vendor/inc",
            "frameworks/flashdb/vendor/fal/inc",
        ],
        "workdir": "frameworks/flashdb/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "KVDB 分区容量(字节)", "type": "number",
             "default": 8192, "min": 2048, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "update", "label": "更新覆盖"},
            {"id": "delete", "label": "删除"},
            {"id": "powerloss", "label": "掉电安全"},
            {"id": "gc", "label": "垃圾回收"},
            {"id": "types", "label": "多类型数据"},
            {"id": "iterate", "label": "遍历迭代"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    {
        "id": "baremetal",
        "name": "裸机结构体配置 (A/B 双备份 + CRC)",
        "category": CATEGORY_BAREMETAL,
        "desc": "最简单的裸机架构：把业务结构体整块映射到 Flash，A/B 双分区交替备份，"
                "CRC32 校验 + 单调序号掉电恢复，无需序列化。附带磨损统计。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/baremetal/bm_config.c",
            "frameworks/baremetal/test/main_baremetal.c",
        ],
        "includes": [
            "simulator",
            "frameworks/baremetal",
        ],
        "workdir": "frameworks/baremetal/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "rounds", "label": "func 压测保存次数", "type": "number",
             "default": 200, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "结构体保存/读取"},
            {"id": "update", "label": "多次更新读最新"},
            {"id": "ab_rotate", "label": "A/B 分区交替"},
            {"id": "powerloss", "label": "掉电回退恢复"},
            {"id": "corrupt", "label": "坏块自动恢复"},
            {"id": "factory", "label": "恢复出厂"},
            {"id": "func", "label": "高频保存压测"},
        ],
    },
    # ---- fast_flashdb_table（轻量 KV/表组件，用户开源组件）----
    {
        "id": "fastflash",
        "_comment": "用户开源组件 fast_flashdb_table：轻量表存储，仅依赖 flash_ops_t 移植接口；"
                    "vendor 源码零修改，移植层对接 simulator 模拟基座",
        "name": "fast_flashdb_table 组件",
        "category": CATEGORY_KV,
        "desc": "轻量表存储组件，支持建表/按索引读写/追加/删除/GC/掉电重放。对接本平台模拟基座。",
        "open_source": True,
        "vendor": "JIANLINGYAN/fast_flashdb_table",
        "repo": "https://github.com/JIANLINGYAN/fast_flashdb_table",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_core.c",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_log.c",
            "frameworks/fastflash/fastflash_sim_port.c",
            "frameworks/fastflash/test/main_fastflash.c",
        ],
        "includes": [
            "simulator",
            "frameworks/fastflash",
            "frameworks/fastflash/vendor/fast_flashdb_table/core",
        ],
        "workdir": "frameworks/fastflash/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "default_env": {
            "type": "0",          # 0=NOR 1=NAND 2=EEPROM（与模拟基座一致）
            "total": "64*1024",
            "erase_size": "4096",
            "write_size": "1",
            "read_us": "50",
            "write_us": "200",
            "erase_us": "40000",
            "erase_cycles": "100000",
            "bad_blocks": "0",
            "bad_ratio": "0",
        },
        "default_config": {"type": 0, "total": 65536, "erase_size": 4096,
                           "write_size": 1, "read_us": 50, "write_us": 200,
                           "erase_us": 40000, "erase_cycles": 100000,
                           "bad_blocks": 0, "bad_ratio": 0},
        "test_env": {"tests": "FLT_TESTS"},
        "test_items": [
            {"id": "init", "label": "初始化/建表"},
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "append", "label": "追加/计数"},
            {"id": "update", "label": "按索引覆盖"},
            {"id": "delete", "label": "删除表"},
            {"id": "gc", "label": "垃圾回收"},
            {"id": "powerloss", "label": "掉电重放"},
        ],
    },
    # ---- Airoha NVDM（KV/裸机持久化组件，厂商专有，经移植层接入模拟基座）----
    {
        "id": "nvdm",
        "_comment": "Airoha NVDM：KV+裸机/RTOS 持久化+分区(PEB)磨损均衡框架；vendor 零修改，"
                    "移植层 nvdm_sim_port.c 实现 nvdm_port_* 契约并桥接 flash_sim",
        "name": "Airoha NVDM (KV/裸机持久化)",
        "category": CATEGORY_KV,
        "desc": "Airoha NVDM 键值存储组件：PEB 磨损均衡、掉电保护状态机、"
                "数据项校验和与垃圾回收。经移植层对接本平台模拟基座。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/nvdm/vendor/src/nvdm_main.c",
            "frameworks/nvdm/vendor/src/nvdm_data.c",
            "frameworks/nvdm/vendor/src/nvdm_io.c",
            "frameworks/nvdm/nvdm_sim_port.c",
            "frameworks/nvdm/test/main_nvdm.c",
        ],
        "includes": [
            "simulator",
            "frameworks/nvdm",
            "frameworks/nvdm/vendor/inc",
        ],
        "cflags": [
            "-DMTK_NVDM_ENABLE",
        ],
        "workdir": "frameworks/nvdm/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "NVDM 区容量(字节)", "type": "number",
             "default": 16384, "min": 2048, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "update", "label": "更新覆盖"},
            {"id": "delete", "label": "删除"},
            {"id": "powerloss", "label": "掉电安全"},
            {"id": "gc", "label": "垃圾回收"},
            {"id": "types", "label": "多类型数据"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    # ---- Zephyr 存储组件（FCB/NVS/ZMS）：vendor 零修改，经共享
    # ---- Zephyr 兼容层（frameworks/zephyr_compat）桥接模拟基座 ----
    {
        "id": "fcb",
        "_comment": "Zephyr FCB：闪存环形缓冲（append-only 日志/事件流 + 回卷覆盖 + "
                    "CRC 校验 + 重启恢复）；适配层以 key 前缀实现 KV 语义",
        "name": "Zephyr FCB (Flash 环形缓冲/KV)",
        "category": CATEGORY_KV,
        "desc": "Zephyr Flash Circular Buffer：append-only 环形日志，写入超容量自动回卷覆盖，"
                "支持遍历/轮转/清空与掉电恢复。适配层以 key 前缀实现 KV 语义。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/fcb/vendor/fcb.c",
            "frameworks/fcb/vendor/fcb_append.c",
            "frameworks/fcb/vendor/fcb_elem_info.c",
            "frameworks/fcb/vendor/fcb_getnext.c",
            "frameworks/fcb/vendor/fcb_rotate.c",
            "frameworks/fcb/vendor/fcb_walk.c",
            "frameworks/fcb/test/main_fcb.c",
        ],
        "includes": [
            "simulator",
            "frameworks/zephyr_compat",
            "frameworks/zephyr_compat/include",
            "frameworks/fcb/vendor/include",
        ],
        "cflags": [
            "-DCONFIG_FLASH_HAS_EXPLICIT_ERASE",
        ],
        "workdir": "frameworks/fcb/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "FCB 区容量(字节)", "type": "number",
             "default": 16384, "min": 2048, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "append", "label": "追加/首条读取"},
            {"id": "walk", "label": "全量遍历"},
            {"id": "rotate", "label": "扇区轮转"},
            {"id": "clear", "label": "清空"},
            {"id": "powerloss", "label": "掉电恢复"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    {
        "id": "nvs",
        "_comment": "Zephyr NVS：ID+数据键值存储（扇区式 ATE 日志 + 掉电安全 + GC），"
                    "vendor 零修改，经 Zephyr 兼容层桥接",
        "name": "Zephyr NVS (KV/裸机持久化)",
        "category": CATEGORY_KV,
        "desc": "Zephyr Non-Volatile Storage：扇区式键值存储，ATE 日志 + CRC 校验，"
                "掉电安全与自动垃圾回收。适配层以 key hash 映射 16 位 ID。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/nvs/vendor/nvs.c",
            "frameworks/nvs/test/main_nvs.c",
        ],
        "includes": [
            "simulator",
            "frameworks/zephyr_compat",
            "frameworks/zephyr_compat/include",
            "frameworks/nvs/vendor/include",
        ],
        "cflags": [
            "-DCONFIG_FLASH_HAS_EXPLICIT_ERASE",
        ],
        "workdir": "frameworks/nvs/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "NVS 区容量(字节)", "type": "number",
             "default": 16384, "min": 2048, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "update", "label": "更新覆盖"},
            {"id": "delete", "label": "删除"},
            {"id": "powerloss", "label": "掉电安全"},
            {"id": "gc", "label": "垃圾回收"},
            {"id": "types", "label": "多类型数据"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    {
        "id": "zms",
        "_comment": "Zephyr ZMS（Zephyr Memory Storage）：固定大小槽位键值存储，"
                    "磨损均衡 + 掉电安全，定位替代 NVS",
        "name": "Zephyr ZMS (KV/固定槽位存储)",
        "category": CATEGORY_KV,
        "desc": "Zephyr Memory Storage：固定大小槽位键值存储，ATE + 数据 CRC，"
                "磨损均衡与掉电安全。适配层以 key hash 映射 32 位 ID。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zms/vendor/zms.c",
            "frameworks/zms/test/main_zms.c",
        ],
        "includes": [
            "simulator",
            "frameworks/zephyr_compat",
            "frameworks/zephyr_compat/include",
            "frameworks/zms/vendor/include",
        ],
        "cflags": [
            "-DCONFIG_FLASH_HAS_EXPLICIT_ERASE",
        ],
        "workdir": "frameworks/zms/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "ZMS 区容量(字节)", "type": "number",
             "default": 16384, "min": 2048, "step": 1024, "group": "test"},
            {"key": "rounds", "label": "功能压测轮数", "type": "number",
             "default": 20, "min": 1, "step": 1, "group": "test"},
        ],
        "test_items": [
            {"id": "write_read", "label": "基础写入/读取"},
            {"id": "update", "label": "更新覆盖"},
            {"id": "delete", "label": "删除"},
            {"id": "powerloss", "label": "掉电安全"},
            {"id": "gc", "label": "垃圾回收"},
            {"id": "types", "label": "多类型数据"},
            {"id": "func", "label": "功能压测(条目表)"},
        ],
    },
    # ---- TYM Setting（Tymphany 厂商组件）：ID 静态表 + RAM 镜像 + 延时整页回写。
    # ---- vendor 经去耦裁剪（去掉 FreeRTOS/ui_shell/hal_log），写放大极大，压测轮数宜小。
    {
        "id": "tym_setting",
        "_comment": "TYM Setting：编译期固定 ID→固定 Flash 地址静态映射 + RAM 全镜像 + "
                    "延时批量整页回写；无磨损均衡/无 GC/掉电不安全，写放大极大",
        "name": "TYM Setting (ID静态表/RAM镜像)",
        "category": CATEGORY_KV,
        "desc": "Tymphany Setting 极简持久化框架：ID 索引 O(1) 访问，延时批量整页回写。"
                "适配层以 key hash 映射 eSettingId，每次写入立即落盘。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/tym_setting/tym_setting_sim_port.c",
            "frameworks/tym_setting/vendor/src/app_setting_idle_activity.c",
            "frameworks/tym_setting/vendor/src/StorageDrv.c",
            "frameworks/tym_setting/test/main_tym_setting.c",
        ],
        "includes": [
            "simulator",
            "frameworks/tym_setting",
            "frameworks/tym_setting/vendor/inc",
            "frameworks/tym_setting/config",
            "frameworks/tym_setting/compat",
        ],
        "cflags": [],
        "workdir": "frameworks/tym_setting/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "capacity", "label": "Setting 区容量(字节)", "type": "number",
             "default": 16384, "min": 2048, "step": 1024, "group": "test"},
        ],
        "test_items": [
            {"id": "basic", "label": "基本写入/读取/修改"},
            {"id": "persist", "label": "延时回写与重启持久化"},
            {"id": "modify", "label": "修改后再次持久化"},
        ],
    },
    {
        "id": "fs",
        "name": "自研文件系统 (fs_store)",
        "category": CATEGORY_FS,
        "desc": "基于模拟基座的简易块式文件系统：文件分配表 + 数据块，支持多文件"
                "读写、某文件频繁修改（原地覆盖/扩展迁移）、追加、删除与查询。",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/fs/fs_store.c",
            "frameworks/fs/test/main_fs.c",
        ],
        "includes": [
            "simulator",
            "frameworks/fs",
        ],
        "workdir": "frameworks/fs/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_schema": [
            {"key": "rounds", "label": "单文件频繁修改轮数", "type": "number",
             "default": 30, "min": 1, "step": 1, "group": "test"},
        ],
        "test_env": {"tests": "FS_TESTS", "rounds": "FS_ROUNDS"},
        "test_items": [
            {"id": "create", "label": "创建多个文件"},
            {"id": "write_read", "label": "多文件写入/读取"},
            {"id": "update", "label": "单文件频繁修改"},
            {"id": "append", "label": "追加写"},
            {"id": "delete", "label": "删除文件"},
            {"id": "query", "label": "大小/存在性查询"},
            {"id": "powerloss", "label": "掉电重放"},
        ],
    },
    {
        "id": "littlefs",
        "name": "LittleFS (开源文件系统)",
        "category": CATEGORY_FS,
        "desc": "littlefs-project/littlefs v2.x：面向嵌入式的小型掉电安全文件系统，"
                "具备掉电保护、磨损均衡与动态磨损感知。通过移植层对接模拟基座。",
        "open_source": True,
        "vendor": "littlefs-project/littlefs",
        "repo": "https://github.com/littlefs-project/littlefs",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/littlefs/littlefs_sim_port.c",
            "frameworks/littlefs/vendor/lfs.c",
            "frameworks/littlefs/vendor/lfs_util.c",
            "frameworks/littlefs/test/main_littlefs.c",
        ],
        "includes": [
            "simulator",
            "frameworks/littlefs",
            "frameworks/littlefs/vendor",
        ],
        "workdir": "frameworks/littlefs/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_env": {"tests": "LFS_TESTS"},
        "test_items": [
            {"id": "mount", "label": "格式化+挂载"},
            {"id": "create", "label": "创建多个文件"},
            {"id": "write_read", "label": "多文件写入/读取"},
            {"id": "update", "label": "单文件频繁修改"},
            {"id": "append", "label": "追加写"},
            {"id": "delete", "label": "删除文件"},
            {"id": "query", "label": "大小/存在性查询"},
            {"id": "powerloss", "label": "掉电重放"},
        ],
    },
    {
        "id": "fatfs",
        "name": "FatFs (开源文件系统)",
        "category": CATEGORY_FS,
        "desc": "ChaN/FatFs R0.16：通用 FAT/exFAT 文件系统模块，经移植层（扇区"
                "读写读改写）对接模拟基座，支持多文件读写、频繁修改、增删查询。",
        "open_source": True,
        "vendor": "elm-chan/FatFs",
        "repo": "https://github.com/abbrev/fatfs",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/fatfs/fatfs_sim_port.c",
            "frameworks/fatfs/vendor/ff.c",
            "frameworks/fatfs/vendor/ffsystem.c",
            "frameworks/fatfs/vendor/ffunicode.c",
            "frameworks/fatfs/test/main_fatfs.c",
        ],
        "includes": [
            "simulator",
            "frameworks/fatfs",
            "frameworks/fatfs/vendor",
        ],
        "workdir": "frameworks/fatfs/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_env": {"tests": "FATFS_TESTS"},
        "test_items": [
            {"id": "mount", "label": "格式化+挂载"},
            {"id": "create", "label": "创建多个文件"},
            {"id": "write_read", "label": "多文件写入/读取"},
            {"id": "update", "label": "单文件频繁修改"},
            {"id": "append", "label": "追加写"},
            {"id": "delete", "label": "删除文件"},
            {"id": "query", "label": "大小/存在性查询"},
            {"id": "powerloss", "label": "掉电重放"},
        ],
    },
    {
        "id": "spiffs",
        "name": "SPIFFS (开源文件系统)",
        "category": CATEGORY_FS,
        "desc": "pellepl/SPIFFS：面向 SPI NOR Flash 的小型文件系统，日志结构 + "
                "垃圾回收，掉电安全。通过移植层对接模拟基座。",
        "open_source": True,
        "vendor": "pellepl/SPIFFS",
        "repo": "https://github.com/pellepl/spiffs",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/spiffs/spiffs_sim_port.c",
            "frameworks/spiffs/vendor/spiffs_nucleus.c",
            "frameworks/spiffs/vendor/spiffs_hydrogen.c",
            "frameworks/spiffs/vendor/spiffs_gc.c",
            "frameworks/spiffs/vendor/spiffs_check.c",
            "frameworks/spiffs/vendor/spiffs_cache.c",
            "frameworks/spiffs/test/main_spiffs.c",
        ],
        "includes": [
            "simulator",
            "frameworks/spiffs",
            "frameworks/spiffs/vendor",
        ],
        "workdir": "frameworks/spiffs/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_env": {"tests": "SPIFFS_TESTS"},
        "test_items": [
            {"id": "mount", "label": "格式化+挂载"},
            {"id": "create", "label": "创建多个文件"},
            {"id": "write_read", "label": "多文件写入/读取"},
            {"id": "update", "label": "单文件频繁修改"},
            {"id": "append", "label": "追加写"},
            {"id": "delete", "label": "删除文件"},
            {"id": "query", "label": "大小/存在性查询"},
            {"id": "powerloss", "label": "掉电重放"},
        ],
    },
    {
        "id": "yaffs",
        "name": "YAFFS (开源文件系统)",
        "category": CATEGORY_FS,
        "desc": "YAFFS2 Direct（GPL v2）：面向 NAND Flash 的日志型文件系统，"
                "磨损均衡 + 掉电保护 + 检查点。经移植层以 chunk+oob 布局"
                "对接模拟基座。",
        "open_source": True,
        "vendor": "Aleph One/YAFFS2",
        "repo": "https://github.com/latelee/yaffs2",
        "sources": [
            "simulator/flash_sim.c",
            "frameworks/yaffs/yaffs_sim_port.c",
            "frameworks/yaffs/vendor/yaffsfs.c",
            "frameworks/yaffs/vendor/yaffs_guts.c",
            "frameworks/yaffs/vendor/yaffs_allocator.c",
            "frameworks/yaffs/vendor/yaffs_tagscompat.c",
            "frameworks/yaffs/vendor/yaffs_tagsmarshall.c",
            "frameworks/yaffs/vendor/yaffs_nand.c",
            "frameworks/yaffs/vendor/yaffs_checkptrw.c",
            "frameworks/yaffs/vendor/yaffs_packedtags1.c",
            "frameworks/yaffs/vendor/yaffs_packedtags2.c",
            "frameworks/yaffs/vendor/yaffs_bitmap.c",
            "frameworks/yaffs/vendor/yaffs_verify.c",
            "frameworks/yaffs/vendor/yaffs_nameval.c",
            "frameworks/yaffs/vendor/yaffs_attribs.c",
            "frameworks/yaffs/vendor/yaffs_yaffs1.c",
            "frameworks/yaffs/vendor/yaffs_yaffs2.c",
            "frameworks/yaffs/vendor/yaffs_ecc.c",
            "frameworks/yaffs/vendor/yaffs_hweight.c",
            "frameworks/yaffs/vendor/yaffs_summary.c",
            "frameworks/yaffs/vendor/yaffs_endian.c",
            "frameworks/yaffs/vendor/yaffs_error.c",
            "frameworks/yaffs/test/main_yaffs.c",
        ],
        "includes": [
            "simulator",
            "frameworks/yaffs",
            "frameworks/yaffs/vendor",
        ],
        "workdir": "frameworks/yaffs/test",
        "config_schema": SIM_CONFIG_SCHEMA,
        "test_env": {"tests": "YAFFS_TESTS"},
        "cflags": [
            "-DCONFIG_YAFFS_DIRECT",
            "-DCONFIG_YAFFS_DEFINES_TYPES",
            "-DCONFIG_YAFFS_PROVIDE_DEFS",
            "-DCONFIG_YAFFSFS_PROVIDE_VALUES",
            "-include",
            "frameworks/yaffs/yaffs_host_types.h",
        ],
        "test_items": [
            {"id": "mount", "label": "挂载"},
            {"id": "create", "label": "创建多个文件"},
            {"id": "write_read", "label": "多文件写入/读取"},
            {"id": "update", "label": "单文件频繁修改"},
            {"id": "append", "label": "追加写"},
            {"id": "delete", "label": "删除文件"},
            {"id": "query", "label": "大小/存在性查询"},
            {"id": "powerloss", "label": "掉电重放"},
        ],
    },
    # 后续框架（ota 等）在此追加注册即可被前端发现
]


# ---------------------------------------------------------------------------
# 统一 HAL 契约（平台无关）：
#   1. 所有框架只依赖 frameworks/common/flash_hal.h；
#   2. 平台内测试/应用层通过 simulator/flash_hal_adapter.c 把模拟基座
#      （flash_sim）桥接为 flash_hal_t 后注册给框架。
# 因此在 get_framework() 中统一注入 include 目录与适配器源码，避免在
# 每个框架的 sources/includes 里重复书写。
# ---------------------------------------------------------------------------
HAL_CONTRACT_INCLUDE = "frameworks/common"     # flash_hal.h
HAL_ADAPTER_SOURCE = "simulator/flash_hal_adapter.c"


def get_framework(fid):
    """按 id 查找框架注册项；未找到返回 None。

    返回前统一注入统一 HAL 契约依赖（include + 适配器源文件），
    保证所有框架（除模拟基座自身）都能按"注册式 HAL"编译。
    """
    f = None
    for x in FRAMEWORKS:
        if x["id"] == fid:
            f = x
            break
    if f is None:
        return None
    # 深拷贝，避免污染全局注册表
    fw = dict(f)
    fw["sources"] = list(f.get("sources", []))
    fw["includes"] = list(f.get("includes", []))
    fw["cflags"] = list(f.get("cflags", []))
    if fid != "simulator":
        if HAL_CONTRACT_INCLUDE not in fw["includes"]:
            fw["includes"].append(HAL_CONTRACT_INCLUDE)
        if HAL_ADAPTER_SOURCE not in fw["sources"]:
            # 适配器放 flash_sim.c 之后、其它源之前均可
            fw["sources"].append(HAL_ADAPTER_SOURCE)
    return fw
