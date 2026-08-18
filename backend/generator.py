#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
backend/generator.py - 代码生成引擎（模块三：可移植库包导出）

职责：根据前端选定的框架与配置参数，生成一份"层次清晰、接口统一、便于
AI 辅助移植"的可移植库文件包：

    <lib>_library/
    ├── README.md               总览：快速上手 + 目录结构 + 文档索引
    ├── PORTING.md              完整移植文档（API / HAL 契约 / 配置 / 编译 /
    │                           集成 / 限制 / 验证）
    ├── HAL_CONTRACT.md         统一适配接口契约（所有框架一致的 Flash 操作抽象）
    ├── AI_PORTING_PROMPT.md    ★ 给"目标工程 AI"的移植提示词（可直接投喂）
    ├── manifest.json           包元信息（供导入校验识别）
    ├── core/                   框架核心（平台无关）或 HAL 参考实现 core/flash_sim.c
    ├── vendor/                 开源/厂商源码（零修改，只读）
    ├── port/                   平台移植层（目标平台替换/重写点）
    ├── config/                 配置文件模板（分区/几何参数）
    ├── include/                对外公共头 + HAL 契约头 include/flash_sim.h
    └── demo/
        ├── test_main.c         标准自检入口（对接 flash_sim.h）
        └── BUILD.md            构建与运行说明（自动生成命令）

设计原则：
  - 仅依赖标准库 zipfile，无需联网/第三方模板引擎。
  - 统一适配接口：所有框架只依赖 include/flash_sim.h 声明的
    read/write/erase 契约，移植 = 按契约实现真实驱动（参考 core/flash_sim.c）。
  - 保留子目录结构（zephyr 系框架依赖 <zephyr/...> 头路径），manifest 记录
    相对路径与 include 目录，供导入/运行闭环使用。
"""

import io
import json
import os
import re
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 包内统一目录（分层）
DIR_CORE = "core"      # 框架核心（平台无关）
DIR_VENDOR = "vendor"  # 开源/厂商源码（零修改）
DIR_PORT = "port"      # 平台移植层（可替换）
DIR_CONFIG = "config"  # 配置文件模板
DIR_INCLUDE = "include"  # 对外公共头 / HAL 契约头
DIR_DEMO = "demo"      # 自检入口 + 构建说明
PACKAGE_DIRS = [DIR_CORE, DIR_VENDOR, DIR_PORT, DIR_CONFIG, DIR_INCLUDE, DIR_DEMO]

# 框架形态
KIND_SELF = "self"       # 自研框架：直接调用 flash_sim 契约
KIND_VENDOR = "vendor"   # 开源/厂商组件：vendor + sim_port 移植层
KIND_ZEPHYR = "zephyr"   # Zephyr 组件：vendor + zephyr_compat 兼容层

_CONFIG_RE = re.compile(r"(_cfg|_config|_conf|conf)\.h$", re.I)


# ---------------------------------------------------------------------------
# 各框架的生成配方：从真实源码复制 + 生成配套文件
# 除原有字段外，新增：
#   kind         self / vendor / zephyr（决定包内文件归类与文档侧重）
#   media        适用介质
#   features     特性清单
#   api          [(签名, 说明), ...] —— 写入 PORTING.md「对外 API」
#   use_hint     最小使用示例（写入 PORTING.md / README.md）
#   hal_map      框架原生底层接口 -> 统一契约的映射说明（写入 HAL_CONTRACT.md）
#   cfg_items    [(参数名, 含义, 默认值), ...] —— 写入 PORTING.md「配置参数」
#   caveats      已知限制（写入 PORTING.md「限制与坑」）
# ---------------------------------------------------------------------------
RECIPES = {
    "kv": {
        "lib_name": "kv_store",
        "title": "KV/NVS 存储框架",
        "kind": KIND_SELF,
        "src_dir": "frameworks/kv",
        "copy_files": ["kv_store.c", "kv_store.h"],
        "desc": "键值存储，两步提交掉电安全、CRC 校验、压实垃圾回收。",
        "media": "NOR / EEPROM（建议 NOR）",
        "features": [
            "两步提交掉电安全（先写数据、后写状态字，掉电记录自动丢弃）",
            "CRC32 校验，识别损坏记录",
            "后写覆盖 + 删除（len=0 记录失效）",
            "压实垃圾回收（GC）",
            "无 RTOS 依赖，纯 C99",
        ],
        "api": [
            ("kv_init(dev, base, size)", "初始化 KV 区并扫描重建 key->offset 索引"),
            ("kv_write(dev, key_id, value, len)", "写入/更新；key_id 0 保留，1~65535 可用"),
            ("kv_read(dev, key_id, value, len)", "读取；len 为出入参，NULL 时仅查询长度"),
            ("kv_delete(dev, key_id)", "删除（写入 len=0 的失效记录）"),
            ("kv_summary(dev, &sum)", "统计有效条目数 / 历史写入记录数"),
            ("kv_used_bytes()", "已用字节数（含历史失效记录）"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);   /* 介质：按 flash_sim.h 契约实现 */\n"
            "kv_init(dev, 0, 8 * 1024);                 /* 0~8KB 为 KV 区 */\n"
            "kv_write(dev, 1, \"hello\", 5);\n"
            "char buf[32]; uint16_t len = sizeof(buf);\n"
            "kv_read(dev, 1, buf, &len);\n"
            "kv_delete(dev, 1);"
        ),
        "hal_map": "kv_store 直接调用契约接口（flash_sim_read/write/erase）与 "
                   "FLASH_CFG_DEFAULTS_BY_TYPE 宏；无需额外移植层，"
                   "移植 = 按 include/flash_sim.h 契约实现真实驱动（参考 core/flash_sim.c）。",
        "cfg_items": [
            ("capacity", "KV 区容量（字节）", "8192"),
            ("page_size", "页大小（字节）", "256"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "KV 区容量须为 erase_size 整数倍",
            "key_id 0 保留，业务从 1 开始",
            "单条 value 上限 KV_MAX_VALUE=256 字节",
            "区域写满且无可回收空间时返回 FLASH_ERR_RANGE，需上层触发 GC/换区",
        ],
    },
    "simulator": {
        "lib_name": "flash_sim",
        "title": "Flash 模拟基座（HAL 参考实现）",
        "kind": KIND_SELF,
        "src_dir": "simulator",
        "copy_files": ["flash_sim.c", "flash_sim.h"],
        "desc": "Flash 物理特性模拟库，提供统一 read/write/erase 接口（即本包 HAL 契约的 PC 参考实现）。",
        "media": "NOR / NAND / EEPROM（仿真）",
        "features": [
            "NOR/NAND 按块擦除、写入仅允许 1->0 的物理语义",
            "EEPROM 字节级直接改写、无擦除概念",
            "寿命（标称擦写次数）/ 固定坏块 / 运行时坏块比率仿真",
            "读写擦耗时仿真，磨损分布与统计接口",
        ],
        "api": [
            ("flash_sim_init(cfg)", "打开/创建介质，返回设备句柄；失败返回 NULL"),
            ("flash_sim_read(dev, off, buf, len)", "随机读 len 字节"),
            ("flash_sim_write(dev, off, buf, len)", "写入（NOR/NAND 前须已擦除）"),
            ("flash_sim_erase(dev, off, len)", "按 erase_size 对齐块擦除"),
            ("flash_sim_deinit(dev)", "关闭介质"),
            ("flash_sim_get_stats/get_wear_map/block_count", "统计与磨损分布（可选）"),
        ],
        "use_hint": (
            "flash_config_t cfg = {0};\n"
            "FLASH_CFG_DEFAULTS_BY_TYPE(cfg, FLASH_TYPE_NOR);\n"
            "cfg.bin_path = \"media.bin\";\n"
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "flash_sim_erase(dev, 0, cfg.erase_size);\n"
            "flash_sim_write(dev, 0, data, len);\n"
            "flash_sim_read(dev, 0, buf, len);\n"
            "flash_sim_deinit(dev);"
        ),
        "hal_map": "本包即为 HAL 参考实现。真实目标上按 include/flash_sim.h 契约重写 "
                   "flash_sim_read/write/erase/deinit（对接真实 SPI/QSPI/NAND 驱动），"
                   "上层框架逻辑完全不用改。",
        "cfg_items": [
            ("type", "介质类型（0=NOR 1=NAND 2=EEPROM）", "0"),
            ("total_size", "总容量（字节）", "64*1024"),
            ("erase_size", "擦除块大小（字节）", "4096"),
            ("write_size", "最小写入单位（字节）", "1"),
            ("erase_cycles", "标称擦写寿命（次）", "100000"),
        ],
        "caveats": [
            "NOR/NAND 写入前必须擦除（仅 1->0）",
            "bin_path 为 PC 仿真文件路径，真实目标实现无需该字段",
            "EEPROM 不支持擦除（flash_sim_erase 返回 FLASH_ERR_NOTSUP）",
        ],
    },
    "easyflash": {
        "lib_name": "easyflash",
        "title": "EasyFlash (ENV/KV 开源组件)",
        "kind": KIND_VENDOR,
        "desc": "成熟开源 KV 框架，内置磨损均衡、掉电保护与垃圾回收。",
        "media": "NOR",
        "features": [
            "ENV/KV 键值存储（字符串 + blob）",
            "磨损均衡（扇区轮转）",
            "掉电保护（多阶段状态提交）",
            "垃圾回收",
        ],
        "src_dir": "frameworks/easyflash",
        "copy_files": [
            "frameworks/easyflash/ef_port.c",
            "frameworks/easyflash/ef_port.h",
            "frameworks/easyflash/ef_cfg.h",
            "frameworks/easyflash/vendor/src/ef_env.c",
            "frameworks/easyflash/vendor/src/ef_utils.c",
            "frameworks/easyflash/vendor/src/easyflash.c",
            "frameworks/easyflash/vendor/inc/easyflash.h",
            "frameworks/easyflash/vendor/inc/ef_def.h",
        ],
        "test_entry": "frameworks/easyflash/test/main_easyflash.c",
        "requires": "flash_sim",
        "api": [
            ("easyflash_init()", "初始化 ENV（须先 ef_port_setup 绑定介质与分区）"),
            ("ef_set_env(key, value) / ef_get_env(key)", "字符串 KV 写入/读取"),
            ("ef_set_env_blob / ef_get_env_blob(key, buf, &len)", "二进制 KV 写入/读取"),
            ("ef_del_env(key)", "删除"),
            ("ef_get_env_write_bytes()", "查询累计写入字节（磨损观测）"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "ef_port_setup(dev, 0, 8 * 1024, 4096, 0);   /* 绑定 ENV 区：基址/容量/擦除块 */\n"
            "easyflash_init();\n"
            "ef_set_env(\"key\", \"value\");\n"
            "char *v = ef_get_env(\"key\");\n"
            "ef_del_env(\"key\");"
        ),
        "hal_map": "ef_port.c 将 EasyFlash 底层环境存取（ef_port_env_read/erase/write 等）"
                   "桥接到统一契约接口。移植 = 保留 vendor/ 零修改，把 port/ef_port.c 的"
                   "底层调用改写为目标驱动（或按契约实现 flash_sim 后直接复用 ef_port.c）。",
        "cfg_items": [
            ("capacity", "ENV 区容量（字节，须 >= 2*erase_size）", "8192"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "ENV 区容量须 >= 2 个擦除块（GC 需要空闲块）",
            "ef_cfg.h 中 EF_ERASE_MIN_SIZE 需与目标擦除块一致",
        ],
    },
    "flashdb": {
        "lib_name": "flashdb",
        "title": "FlashDB (KVDB 开源组件)",
        "kind": KIND_VENDOR,
        "desc": "EasyFlash 作者的下一代 KV 框架，具备磨损均衡、掉电保护与 GC。",
        "media": "NOR",
        "features": [
            "KVDB 键值存储（字符串 + blob + 遍历）",
            "磨损均衡 + 掉电保护 + 垃圾回收",
            "基于 FAL 抽象层，分区可配置",
        ],
        "src_dir": "frameworks/flashdb",
        "copy_files": [
            "frameworks/flashdb/fal_flash_sim_port.c",
            "frameworks/flashdb/fal_flash_sim_port.h",
            "frameworks/flashdb/fdb_cfg.h",
            "frameworks/flashdb/fal_cfg.h",
            "frameworks/flashdb/vendor/src/fdb.c",
            "frameworks/flashdb/vendor/src/fdb_utils.c",
            "frameworks/flashdb/vendor/src/fdb_kvdb.c",
            "frameworks/flashdb/vendor/src/fdb_tsdb.c",
            "frameworks/flashdb/vendor/src/fdb_file.c",
            "frameworks/flashdb/vendor/inc/flashdb.h",
            "frameworks/flashdb/vendor/inc/fdb_def.h",
            "frameworks/flashdb/vendor/inc/fdb_low_lvl.h",
            "frameworks/flashdb/vendor/fal/src/fal.c",
            "frameworks/flashdb/vendor/fal/src/fal_flash.c",
            "frameworks/flashdb/vendor/fal/src/fal_partition.c",
            "frameworks/flashdb/vendor/fal/inc/fal.h",
            "frameworks/flashdb/vendor/fal/inc/fal_def.h",
        ],
        "test_entry": "frameworks/flashdb/test/main_flashdb.c",
        "requires": "flash_sim",
        "api": [
            ("fal_sim_port_init(dev, total, erase, off, len, verbose)", "绑定介质 + FAL 初始化 + 安装 KVDB 分区"),
            ("fdb_kvdb_init(&db, name, part_name, ...)", "在分区上创建/打开 KVDB"),
            ("fdb_kv_set / fdb_kv_get(db, key, value)", "字符串 KV"),
            ("fdb_kv_set_blob / fdb_kv_get_blob(db, key, blob)", "二进制 KV"),
            ("fdb_kv_del(db, key) / fdb_kv_iterate(db, itr)", "删除 / 遍历"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "fal_sim_port_init(dev, 64 * 1024, 4096, 0, 8 * 1024, 0);  /* 绑定+FAL+分区 */\n"
            "fdb_kvdb_t db;\n"
            "fdb_kvdb_init(&db, \"cfg\", FAL_KVDB_PART_NAME, NULL, NULL);\n"
            "fdb_kv_set(&db, \"key\", \"value\");\n"
            "char *v = fdb_kv_get(&db, \"key\");\n"
            "fdb_kv_del(&db, \"key\");"
        ),
        "hal_map": "fal_flash_sim_port.c 实现 FAL 的 flash 操作（fal_flash_read/write/erase/"
                   "init），桥接到统一契约接口。移植 = 保留 vendor/ 零修改，改 port/ 层对接"
                   "目标驱动，并在 fdb_cfg.h/fal_cfg.h 配置分区表。",
        "cfg_items": [
            ("capacity", "KVDB 分区容量（字节，须 >= 2*erase_size）", "8192"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "KVDB 分区须在 fal_cfg.h 的分区表中登记",
            "分区容量须 >= 2 个擦除块",
            "fdb 使用动态内存，需提供 malloc/free",
        ],
    },
    "baremetal": {
        "lib_name": "bm_config",
        "title": "裸机结构体配置 (A/B 双备份 + CRC)",
        "kind": KIND_SELF,
        "desc": "最简单的裸机架构：结构体整块映射，A/B 双备份 + CRC32 + 单调序号掉电恢复。",
        "media": "NOR",
        "features": [
            "业务结构体整块映射，无需序列化",
            "A/B 双分区交替备份，磨损分摊",
            "CRC32 校验 + 单调序号判断最新，掉电安全",
            "坏块自动回退到另一分区",
        ],
        "src_dir": "frameworks/baremetal",
        "copy_files": [
            "frameworks/baremetal/bm_config.c",
            "frameworks/baremetal/bm_config.h",
        ],
        "test_entry": "frameworks/baremetal/test/main_baremetal.c",
        "requires": "flash_sim",
        "api": [
            ("bm_config_init(&ctx, dev, base_a, base_b, part_size, payload_len)", "绑定 A/B 分区并扫描确定当前生效者"),
            ("bm_config_save(&ctx, payload)", "保存配置（写入较旧分区，A/B 轮换）"),
            ("bm_config_load(&ctx, out)", "读取当前生效配置"),
            ("bm_config_reset(&ctx)", "恢复出厂（擦除两个分区）"),
            ("bm_config_status(&ctx, &st)", "分区健康状态诊断"),
            ("bm_crc32(data, len)", "CRC32（对外暴露）"),
        ],
        "use_hint": (
            "bm_config_t ctx;\n"
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "bm_config_init(&ctx, dev, 0, 4096, 4096, sizeof(my_cfg));  /* A=0 B=4096 */\n"
            "bm_config_save(&ctx, &my_cfg);\n"
            "bm_config_load(&ctx, &my_cfg);"
        ),
        "hal_map": "bm_config 直接调用契约接口（flash_sim_erase/write/read）实现 A/B 轮换。"
                   "移植 = 按契约实现真实驱动即可，框架零改动。",
        "cfg_items": [
            ("part_size", "单分区大小（字节，>= 头部+payload）", "4096"),
            ("payload_len", "配置体长度（<= BM_MAX_PAYLOAD=1024）", "64"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "payload 上限 BM_MAX_PAYLOAD=1024 字节",
            "两个分区须块对齐且互不重叠",
            "适合低频保存的静态配置，写放大 2x",
        ],
    },
    "fs": {
        "lib_name": "fs_store",
        "title": "自研文件系统 (fs_store)",
        "kind": KIND_SELF,
        "desc": "简易块式文件系统：文件分配表 + 数据块，多文件读写/频繁修改/追加/删除/查询。",
        "media": "NOR",
        "features": [
            "文件分配表（FAT）+ 数据块布局，多文件管理",
            "覆盖写原地复用；扩展时两步提交迁移",
            "追加写优先利用页尾空闲字节",
            "掉电模型：FAT 后写提交，迁移不丢数据",
        ],
        "src_dir": "frameworks/fs",
        "copy_files": [
            "frameworks/fs/fs_store.c",
            "frameworks/fs/fs_store.h",
        ],
        "test_entry": "frameworks/fs/test/main_fs.c",
        "requires": "flash_sim",
        "api": [
            ("fs_init(dev, base, size, block_size)", "初始化 FS（block_size 须=底层 erase_size）"),
            ("fs_format(dev, base, size, block_size)", "格式化重建空 FAT"),
            ("fs_create / fs_delete(dev, name)", "创建/删除文件"),
            ("fs_write(dev, name, buf, len)", "覆盖写（不存在则创建）"),
            ("fs_append(dev, name, buf, len)", "追加写"),
            ("fs_read(dev, name, buf, offset, &len)", "按偏移读，len 为出入参"),
            ("fs_get_size / fs_exists / fs_file_count", "查询"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "fs_init(dev, 0, 32 * 1024, 4096);\n"
            "fs_create(dev, \"cfg.txt\");\n"
            "fs_write(dev, \"cfg.txt\", \"hello\", 5);\n"
            "char buf[32]; uint32_t len = sizeof(buf);\n"
            "fs_read(dev, \"cfg.txt\", buf, 0, &len);\n"
            "fs_delete(dev, \"cfg.txt\");"
        ),
        "hal_map": "fs_store 直接调用契约接口（flash_sim_erase/write/read）。"
                   "移植 = 按契约实现真实驱动即可，框架零改动。",
        "cfg_items": [
            ("capacity", "FS 区容量（字节，>= 2 块）", "32768"),
            ("erase_size", "块大小（= 底层擦除块，字节）", "4096"),
        ],
        "caveats": [
            "文件名上限 FS_NAME_MAX=16（含 '\\0'）",
            "原地覆盖写不做原子性保证（裸机覆盖写语义）",
            "掉电安全仅对「扩展迁移」场景保证（两步提交）",
        ],
    },
    "littlefs": {
        "lib_name": "littlefs",
        "title": "LittleFS (开源文件系统)",
        "kind": KIND_VENDOR,
        "desc": "littlefs-project/littlefs v2.x：嵌入式掉电安全文件系统，含磨损均衡。",
        "media": "NOR / NAND",
        "features": [
            "掉电安全（copy-on-write）",
            "静态+动态磨损均衡",
            "目录/文件标准 POSIX 风格 API",
        ],
        "src_dir": "frameworks/littlefs",
        "copy_files": [
            "frameworks/littlefs/littlefs_sim_port.c",
            "frameworks/littlefs/littlefs_sim_port.h",
            "frameworks/littlefs/vendor/lfs.c",
            "frameworks/littlefs/vendor/lfs.h",
            "frameworks/littlefs/vendor/lfs_util.c",
            "frameworks/littlefs/vendor/lfs_util.h",
        ],
        "test_entry": "frameworks/littlefs/test/main_littlefs.c",
        "requires": "flash_sim",
        "api": [
            ("littlefs_sim_init_device(bin_path, &cfg)", "打开介质并填充 lfs_config（回调+几何）"),
            ("lfs_mount(&lfs, &cfg)", "挂载（首次 lfs_format）"),
            ("lfs_file_open/write/read/close", "文件读写"),
            ("lfs_mkdir / lfs_remove / lfs_stat", "目录/删除/查询"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "struct lfs_config lc;\n"
            "littlefs_sim_init_device(\"lfs.bin\", &lc);\n"
            "lfs_t lfs; lfs_format(&lfs, &lc);\n"
            "lfs_mount(&lfs, &lc);\n"
            "lfs_file_t f; lfs_file_open(&lfs, &f, \"a.txt\", LFS_O_RDWR | LFS_O_CREAT);\n"
            "lfs_file_write(&lfs, &f, \"hi\", 2); lfs_file_close(&lfs, &f);"
        ),
        "hal_map": "littlefs_sim_port.c 实现 lfs_config 的 read/prog/erase/sync 四个回调，"
                   "桥接到统一契约接口（其中 erase 语义需同步给 LittleFS 的 block_cycles）。"
                   "移植 = 保留 vendor/ 零修改，重写 port/ 层对接到目标驱动。",
        "cfg_items": [
            ("capacity", "FS 区容量（字节）", "32768"),
            ("erase_size", "块大小（= 底层擦除块，字节）", "4096"),
        ],
        "caveats": [
            "block_size 必须等于目标擦除块大小",
            "需要 RAM：lfs_t + 2 个 prog/read buffer（默认 256B 各一）",
            "lfs 默认最大文件数/目录深度见 lfs_util.h 配置",
        ],
    },
    "fatfs": {
        "lib_name": "fatfs",
        "title": "FatFs (开源文件系统)",
        "kind": KIND_VENDOR,
        "desc": "ChaN/FatFs R0.16：通用 FAT 文件系统模块，经移植层对接模拟基座。",
        "media": "NOR / NAND（按扇区读写）",
        "features": [
            "FAT12/16/32 通用格式",
            "标准 f_open/f_write/f_read 接口",
            "兼容性极好，可插拔 diskio",
        ],
        "src_dir": "frameworks/fatfs",
        "copy_files": [
            "frameworks/fatfs/fatfs_sim_port.c",
            "frameworks/fatfs/fatfs_sim_port.h",
            "frameworks/fatfs/vendor/ff.c",
            "frameworks/fatfs/vendor/ff.h",
            "frameworks/fatfs/vendor/ffconf.h",
            "frameworks/fatfs/vendor/ffsystem.c",
            "frameworks/fatfs/vendor/ffunicode.c",
            "frameworks/fatfs/vendor/diskio.h",
        ],
        "test_entry": "frameworks/fatfs/test/main_fatfs.c",
        "requires": "flash_sim",
        "api": [
            ("fatfs_sim_init_device(bin_path)", "打开介质并返回扇区大小"),
            ("f_mount(&fs, \"0:\", 1)", "挂载卷（首次 f_mkfs 格式化）"),
            ("f_open / f_write / f_read / f_close", "文件读写"),
            ("f_mkdir / f_unlink / f_stat", "目录/删除/查询"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "fatfs_sim_init_device(\"fat.bin\");\n"
            "FATFS fs; f_mount(&fs, \"0:\", 1);\n"
            "f_mkfs(\"0:\", FM_FAT | FM_SFD, 0, work, sizeof(work));  /* 首次 */\n"
            "f_mount(&fs, \"0:\", 1);\n"
            "FIL f; f_open(&f, \"0:a.txt\", FA_WRITE | FA_CREATE_ALWAYS);\n"
            "f_write(&f, \"hi\", 2, &bw); f_close(&f);"
        ),
        "hal_map": "fatfs_sim_port.c 实现 diskio 的 disk_read/disk_write/disk_ioctl/"
                   "disk_status，以扇区为单位桥接统一契约接口（读改写）。"
                   "移植 = 保留 vendor/ 零修改，重写 port/ 层对接到目标驱动。",
        "cfg_items": [
            ("capacity", "卷容量（字节）", "131072"),
            ("sector_size", "扇区大小（字节）", "512"),
        ],
        "caveats": [
            "扇区读写以 512B 为单位，内部读改写（写放大）",
            "ffconf.h 中 FF_USE_MKFS / FF_FS_READONLY 等按需配置",
            "挂载前需 f_mkfs 格式化；掉电安全弱于日志型 FS",
        ],
    },
    "spiffs": {
        "lib_name": "spiffs",
        "title": "SPIFFS (开源文件系统)",
        "kind": KIND_VENDOR,
        "desc": "pellepl/SPIFFS：面向 SPI NOR Flash 的小型掉电安全文件系统。",
        "media": "NOR",
        "features": [
            "日志结构 + 垃圾回收",
            "掉电安全",
            "面向小容量 SPI NOR 优化",
        ],
        "src_dir": "frameworks/spiffs",
        "copy_files": [
            "frameworks/spiffs/spiffs_sim_port.c",
            "frameworks/spiffs/spiffs_sim_port.h",
            "frameworks/spiffs/spiffs_config.h",
            "frameworks/spiffs/vendor/spiffs_nucleus.c",
            "frameworks/spiffs/vendor/spiffs_nucleus.h",
            "frameworks/spiffs/vendor/spiffs_hydrogen.c",
            "frameworks/spiffs/vendor/spiffs_gc.c",
            "frameworks/spiffs/vendor/spiffs_check.c",
            "frameworks/spiffs/vendor/spiffs_cache.c",
            "frameworks/spiffs/vendor/spiffs.h",
        ],
        "test_entry": "frameworks/spiffs/test/main_spiffs.c",
        "requires": "flash_sim",
        "api": [
            ("spiffs_sim_init_device(bin_path) / spiffs_sim_mount()", "打开介质并挂载"),
            ("spiffs_sim_format()", "格式化"),
            ("SPIFFS_open/write/read/close", "文件读写"),
            ("SPIFFS_remove / SPIFFS_rename / SPIFFS_stat", "删除/改名/查询"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "spiffs_sim_init_device(\"spiffs.bin\");\n"
            "if (spiffs_sim_mount() != 0) { spiffs_sim_format(); spiffs_sim_mount(); }\n"
            "spiffs_file fd = SPIFFS_open(fs, \"a.txt\", SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR, 0);\n"
            "SPIFFS_write(fs, fd, \"hi\", 2); SPIFFS_close(fs, fd);"
        ),
        "hal_map": "spiffs_sim_port.c 实现 SPIFFS HAL 回调（spiffs_hal_read/write/erase）"
                   "并配置 spiffs_config.h 的页/块几何。移植 = 保留 vendor/ 零修改，"
                   "重写 port/ 层对接到目标驱动，并按目标几何调整 config/。",
        "cfg_items": [
            ("capacity", "FS 区容量（字节）", "65536"),
            ("log_page_size", "逻辑页大小（字节，须与 spiffs_config 一致）", "256"),
            ("erase_size", "块大小（= 底层擦除块，字节）", "4096"),
        ],
        "caveats": [
            "spiffs_config.h 的页/块几何必须与介质一致",
            "日志结构有约 30~50% 空间开销",
            "不适合超多小文件（页粒度分配）",
        ],
    },
    "yaffs": {
        "lib_name": "yaffs",
        "title": "YAFFS (开源文件系统)",
        "kind": KIND_VENDOR,
        "desc": "YAFFS2 Direct：面向 NAND Flash 的日志型文件系统（GPL v2）。",
        "media": "NAND（chunk + oob/spare 布局）",
        "features": [
            "NAND 日志型文件系统，专为坏块/位翻转设计",
            "检查点加速挂载",
            "磨损均衡 + 掉电保护",
            "chunk + oob(spare) 双区布局",
        ],
        "src_dir": "frameworks/yaffs",
        "copy_files": [
            "frameworks/yaffs/yaffs_sim_port.c",
            "frameworks/yaffs/yaffs_sim_port.h",
            "frameworks/yaffs/yaffs_host_types.h",
            "frameworks/yaffs/vendor/yaffsfs.c",
            "frameworks/yaffs/vendor/yaffsfs.h",
            "frameworks/yaffs/vendor/yaffs_guts.c",
            "frameworks/yaffs/vendor/yaffs_guts.h",
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
            "frameworks/yaffs/vendor/yaffs_allocator.h",
            "frameworks/yaffs/vendor/yaffs_attribs.h",
            "frameworks/yaffs/vendor/yaffs_bitmap.h",
            "frameworks/yaffs/vendor/yaffs_checkptrw.h",
            "frameworks/yaffs/vendor/yaffs_ecc.h",
            "frameworks/yaffs/vendor/yaffs_endian.h",
            "frameworks/yaffs/vendor/yaffs_flashif.h",
            "frameworks/yaffs/vendor/yaffs_flashif2.h",
            "frameworks/yaffs/vendor/yaffs_getblockinfo.h",
            "frameworks/yaffs/vendor/yaffs_hweight.h",
            "frameworks/yaffs/vendor/yaffs_list.h",
            "frameworks/yaffs/vendor/yaffs_nameval.h",
            "frameworks/yaffs/vendor/yaffs_nand.h",
            "frameworks/yaffs/vendor/yaffs_nandemul2k.h",
            "frameworks/yaffs/vendor/yaffs_osglue.h",
            "frameworks/yaffs/vendor/yaffs_packedtags1.h",
            "frameworks/yaffs/vendor/yaffs_packedtags2.h",
            "frameworks/yaffs/vendor/yaffs_summary.h",
            "frameworks/yaffs/vendor/yaffs_tagscompat.h",
            "frameworks/yaffs/vendor/yaffs_tagsmarshall.h",
            "frameworks/yaffs/vendor/yaffs_trace.h",
            "frameworks/yaffs/vendor/yaffs_verify.h",
            "frameworks/yaffs/vendor/yaffs_yaffs1.h",
            "frameworks/yaffs/vendor/yaffs_yaffs2.h",
            "frameworks/yaffs/vendor/yaffscfg.h",
            "frameworks/yaffs/vendor/ydirectenv.h",
            "frameworks/yaffs/vendor/yportenv.h",
        ],
        "test_entry": "frameworks/yaffs/test/main_yaffs.c",
        "cflags": [
            "-DCONFIG_YAFFS_DIRECT",
            "-DCONFIG_YAFFS_DEFINES_TYPES",
            "-DCONFIG_YAFFS_PROVIDE_DEFS",
            "-DCONFIG_YAFFSFS_PROVIDE_VALUES",
            "-include",
            "frameworks/yaffs/yaffs_host_types.h",
        ],
        "requires": "flash_sim",
        "api": [
            ("yaffs_sim_init_device(bin_path) / yaffs_sim_start_up()", "打开 NAND 介质并启动 YAFFS"),
            ("yaffs_mount(path)", "挂载文件系统"),
            ("yaffs_open / yaffs_write / yaffs_read / yaffs_close", "文件读写"),
            ("yaffs_unlink / yaffs_mkdir / yaffs_stat", "删除/目录/查询"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "yaffs_sim_init_device(\"yaffs.bin\");\n"
            "yaffs_sim_start_up();\n"
            "yaffs_mount(\"/\");\n"
            "int fd = yaffs_open(\"/a.txt\", O_CREAT | O_RDWR, 0666);\n"
            "yaffs_write(fd, \"hi\", 2); yaffs_close(fd);"
        ),
        "hal_map": "yaffs_sim_port.c 实现 yaffs 的 NAND 驱动接口（chunk 读/写 + oob/spare + "
                   "块擦除），桥接到统一契约接口，并按 2KB chunk+32B oob 布局映射。"
                   "移植 = 保留 vendor/ 零修改，重写 port/ 层对接目标 NAND 控制器（含 ECC/坏块）。",
        "cfg_items": [
            ("chunk_size", "chunk 大小（字节，见 yaffs_sim_port.h）", "2048"),
            ("oob_size", "spare/oob 大小（字节）", "32"),
            ("chunks_per_block", "每块 chunk 数", "32"),
        ],
        "caveats": [
            "面向 NAND：chunk+oob 布局，EEPROM 不适用",
            "GPL v2 许可证，商用注意合规",
            "编译需 -D 宏 + -include yaffs_host_types.h（见 demo/BUILD.md）",
        ],
    },
    # ---- Zephyr 存储组件：共享 zephyr_compat 兼容层，导出时整体打包 ----
    "fcb": {
        "lib_name": "zephyr_fcb",
        "title": "Zephyr FCB (闪存环形缓冲)",
        "kind": KIND_ZEPHYR,
        "desc": "Zephyr Flash Circular Buffer：append-only 环形日志 + 回卷覆盖 + CRC + 掉电恢复。",
        "media": "NOR",
        "features": [
            "append-only 环形日志（事件流）",
            "写满自动回卷覆盖",
            "CRC 校验 + 掉电恢复",
            "遍历 / 轮转 / 清空",
        ],
        "src_dir": "frameworks/fcb",
        "copy_files": [
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zephyr_compat/zephyr_compat.h",
            "frameworks/zephyr_compat/include/zephyr/device.h",
            "frameworks/zephyr_compat/include/zephyr/kernel.h",
            "frameworks/zephyr_compat/include/zephyr/toolchain.h",
            "frameworks/zephyr_compat/include/zephyr/types.h",
            "frameworks/zephyr_compat/include/zephyr/drivers/flash.h",
            "frameworks/zephyr_compat/include/zephyr/storage/flash_map.h",
            "frameworks/zephyr_compat/include/zephyr/sys/__assert.h",
            "frameworks/zephyr_compat/include/zephyr/sys/crc.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_internal.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_macro.h",
            "frameworks/zephyr_compat/include/zephyr/logging/log.h",
            "frameworks/fcb/vendor/fcb.c",
            "frameworks/fcb/vendor/fcb_append.c",
            "frameworks/fcb/vendor/fcb_elem_info.c",
            "frameworks/fcb/vendor/fcb_getnext.c",
            "frameworks/fcb/vendor/fcb_rotate.c",
            "frameworks/fcb/vendor/fcb_walk.c",
            "frameworks/fcb/vendor/fcb_priv.h",
            "frameworks/fcb/vendor/include/zephyr/fs/fcb.h",
        ],
        "test_entry": "frameworks/fcb/test/main_fcb.c",
        "cflags": ["-DCONFIG_FLASH_HAS_EXPLICIT_ERASE"],
        "requires": "flash_sim",
        "api": [
            ("zephyr_compat_register_flash(dev, erase, write_size, 0xFF)", "注册介质为 Zephyr flash 设备"),
            ("zephyr_compat_register_area(id, zdev, off, size)", "注册分区"),
            ("fcb_init(area_id, &fcb)", "初始化环形缓冲"),
            ("fcb_append(&fcb, len, &loc)", "追加一条记录"),
            ("fcb_getnext(&fcb, &loc)", "顺序遍历"),
            ("fcb_rotate / fcb_clear", "轮转 / 清空"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "const struct device *zdev = zephyr_compat_register_flash(dev, 4096, 1, 0xFF);\n"
            "zephyr_compat_register_area(0, zdev, 0, 16 * 1024);\n"
            "struct fcb fcb; fcb_init(0, &fcb);\n"
            "struct fcb_entry loc; fcb_append(&fcb, 8, &loc);   /* 之后写 loc.fe_data */\n"
            "fcb_getnext(&fcb, &loc);                           /* 遍历 */"
        ),
        "hal_map": "zephyr_compat 兼容层把 Zephyr flash 设备 API / flash_area 分区 / k_mutex / "
                   "CRC 桥接到统一契约接口；vendor 零修改。移植 = 保留 vendor/ 与 port/zephyr_compat，"
                   "仅需按契约实现真实驱动（参考 core/flash_sim.c）。",
        "cfg_items": [
            ("capacity", "FCB 区容量（字节）", "16384"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "FCB 是 append-only 日志，不适合高频覆盖写（回卷覆盖会丢旧记录）",
            "需 -DCONFIG_FLASH_HAS_EXPLICIT_ERASE",
            "适配层以 key 前缀实现 KV 语义（见 demo/test_main.c）",
        ],
    },
    "nvs": {
        "lib_name": "zephyr_nvs",
        "title": "Zephyr NVS (键值存储)",
        "kind": KIND_ZEPHYR,
        "desc": "Zephyr Non-Volatile Storage：ID+数据键值存储，ATE 日志 + 掉电安全 + GC。",
        "media": "NOR",
        "features": [
            "ID 键值存储（16 位 ID）",
            "扇区式 ATE 日志 + CRC",
            "掉电安全 + 自动垃圾回收",
        ],
        "src_dir": "frameworks/nvs",
        "copy_files": [
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zephyr_compat/zephyr_compat.h",
            "frameworks/zephyr_compat/include/zephyr/device.h",
            "frameworks/zephyr_compat/include/zephyr/kernel.h",
            "frameworks/zephyr_compat/include/zephyr/toolchain.h",
            "frameworks/zephyr_compat/include/zephyr/types.h",
            "frameworks/zephyr_compat/include/zephyr/drivers/flash.h",
            "frameworks/zephyr_compat/include/zephyr/storage/flash_map.h",
            "frameworks/zephyr_compat/include/zephyr/sys/__assert.h",
            "frameworks/zephyr_compat/include/zephyr/sys/crc.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_internal.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_macro.h",
            "frameworks/zephyr_compat/include/zephyr/logging/log.h",
            "frameworks/nvs/vendor/nvs.c",
            "frameworks/nvs/vendor/nvs_priv.h",
            "frameworks/nvs/vendor/include/zephyr/kvss/nvs.h",
        ],
        "test_entry": "frameworks/nvs/test/main_nvs.c",
        "cflags": ["-DCONFIG_FLASH_HAS_EXPLICIT_ERASE"],
        "requires": "flash_sim",
        "api": [
            ("zephyr_compat_register_flash(dev, erase, write_size, 0xFF)", "注册介质为 Zephyr flash 设备"),
            ("nvs_mount(&fs)", "挂载（fs.offset/sector_size/sector_count 需配置）"),
            ("nvs_write(&fs, id, data, len)", "写入（len=0 等效删除）"),
            ("nvs_read(&fs, id, data, len)", "读取"),
            ("nvs_delete(&fs, id)", "删除"),
            ("nvs_calc_free_space(&fs)", "剩余空间"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "zephyr_compat_register_flash(dev, 4096, 1, 0xFF);\n"
            "struct nvs_fs fs = { .offset = 0, .sector_size = 4096, .sector_count = 4 };\n"
            "nvs_mount(&fs);\n"
            "nvs_write(&fs, 1, \"hello\", 5);\n"
            "char buf[32]; nvs_read(&fs, 1, buf, sizeof(buf));"
        ),
        "hal_map": "zephyr_compat 兼容层把 Zephyr flash API 桥接到统一契约接口；vendor 零修改。"
                   "移植 = 保留 vendor/ 与 port/zephyr_compat，按契约实现真实驱动即可。",
        "cfg_items": [
            ("capacity", "NVS 区容量（字节）", "16384"),
            ("erase_size", "扇区大小（= 擦除块，字节）", "4096"),
        ],
        "caveats": [
            "sector_count * sector_size = 分区容量",
            "id 16 位；适配层以 key hash 映射 ID（见 demo/test_main.c）",
            "需 -DCONFIG_FLASH_HAS_EXPLICIT_ERASE",
        ],
    },
    "zms": {
        "lib_name": "zephyr_zms",
        "title": "Zephyr ZMS (固定槽位键值存储)",
        "kind": KIND_ZEPHYR,
        "desc": "Zephyr Memory Storage：固定大小槽位键值存储，磨损均衡 + 掉电安全。",
        "media": "NOR",
        "features": [
            "固定槽位键值存储（Zephyr 官方，定位替代 NVS）",
            "磨损均衡 + 掉电安全",
            "数据 CRC + 遍历/历史读取",
        ],
        "src_dir": "frameworks/zms",
        "copy_files": [
            "frameworks/zephyr_compat/zephyr_compat.c",
            "frameworks/zephyr_compat/zephyr_compat.h",
            "frameworks/zephyr_compat/include/zephyr/device.h",
            "frameworks/zephyr_compat/include/zephyr/kernel.h",
            "frameworks/zephyr_compat/include/zephyr/toolchain.h",
            "frameworks/zephyr_compat/include/zephyr/types.h",
            "frameworks/zephyr_compat/include/zephyr/drivers/flash.h",
            "frameworks/zephyr_compat/include/zephyr/storage/flash_map.h",
            "frameworks/zephyr_compat/include/zephyr/sys/__assert.h",
            "frameworks/zephyr_compat/include/zephyr/sys/crc.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_internal.h",
            "frameworks/zephyr_compat/include/zephyr/sys/util_macro.h",
            "frameworks/zephyr_compat/include/zephyr/logging/log.h",
            "frameworks/zms/vendor/zms.c",
            "frameworks/zms/vendor/zms_priv.h",
            "frameworks/zms/vendor/include/zephyr/kvss/zms.h",
        ],
        "test_entry": "frameworks/zms/test/main_zms.c",
        "cflags": ["-DCONFIG_FLASH_HAS_EXPLICIT_ERASE"],
        "requires": "flash_sim",
        "api": [
            ("zephyr_compat_register_flash(dev, erase, write_size, 0xFF)", "注册介质为 Zephyr flash 设备"),
            ("zms_mount(&fs)", "挂载（fs.offset/sector_size/sector_count 需配置）"),
            ("zms_write(&fs, id, data, len)", "写入（len=0 等效删除）"),
            ("zms_read(&fs, id, data, len)", "读取"),
            ("zms_delete(&fs, id)", "删除"),
            ("zms_get_num_cycles(&fs, &cycles)", "磨损/循环计数统计"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "zephyr_compat_register_flash(dev, 4096, 1, 0xFF);\n"
            "struct zms_fs fs = { .offset = 0, .sector_size = 4096, .sector_count = 4 };\n"
            "zms_mount(&fs);\n"
            "zms_write(&fs, 1, \"hello\", 5);\n"
            "char buf[32]; zms_read(&fs, 1, buf, sizeof(buf));"
        ),
        "hal_map": "zephyr_compat 兼容层把 Zephyr flash API 桥接到统一契约接口；vendor 零修改。"
                   "移植 = 保留 vendor/ 与 port/zephyr_compat，按契约实现真实驱动即可。",
        "cfg_items": [
            ("capacity", "ZMS 区容量（字节）", "16384"),
            ("erase_size", "扇区大小（= 擦除块，字节）", "4096"),
        ],
        "caveats": [
            "固定槽位：写放大中等，适合配置类数据",
            "适配层以 key hash 映射 ID（见 demo/test_main.c）",
            "需 -DCONFIG_FLASH_HAS_EXPLICIT_ERASE",
        ],
    },
    # ---- TYM Setting（Tymphany 厂商组件）：ID 静态表 + RAM 镜像 + 整页回写 ----
    "tym_setting": {
        "lib_name": "tym_setting",
        "title": "TYM Setting (ID静态表/RAM镜像)",
        "kind": KIND_VENDOR,
        "desc": "Tymphany Setting：编译期固定 ID→地址静态映射，RAM 全镜像 + 延时批量整页回写。",
        "media": "NOR",
        "features": [
            "ID 索引 O(1) 访问（静态表）",
            "RAM 全镜像 + 延时批量整页回写",
            "已去 FreeRTOS / ui_shell / hal_log 耦合",
        ],
        "src_dir": "frameworks/tym_setting",
        "copy_files": [
            "frameworks/tym_setting/tym_setting_sim_port.c",
            "frameworks/tym_setting/tym_setting_sim_port.h",
            "frameworks/tym_setting/vendor/src/app_setting_idle_activity.c",
            "frameworks/tym_setting/vendor/src/StorageDrv.c",
            "frameworks/tym_setting/vendor/inc/app_setting_idle_activity.h",
            "frameworks/tym_setting/vendor/inc/SettingSrv_priv.h",
            "frameworks/tym_setting/vendor/inc/StorageDrv.h",
            "frameworks/tym_setting/vendor/inc/StorageDrv_priv.h",
            "frameworks/tym_setting/vendor/inc/NvmDrv.h",
            "frameworks/tym_setting/vendor/inc/NvmDrv_priv.h",
            "frameworks/tym_setting/config/setting_id.h",
            "frameworks/tym_setting/config/SettingSrv.config",
            "frameworks/tym_setting/compat/cplus.h",
            "frameworks/tym_setting/compat/commonTypes.h",
            "frameworks/tym_setting/compat/tym_setting_log.h",
        ],
        "test_entry": "frameworks/tym_setting/test/main_tym_setting.c",
        "requires": "flash_sim",
        "api": [
            ("tym_setting_sim_setup(dev, base, capacity, erase_size)", "注入介质与分区（须在 SettingSrv_Init 前）"),
            ("SettingSrv_Init()", "初始化并加载 RAM 镜像"),
            ("SettingSrv_Set(eSettingId, &data, len)", "写设置（O(1) 索引）"),
            ("SettingSrv_Get(eSettingId, &data, len)", "读设置"),
            ("SettingSrv_Flush() / idle 延时回写", "整页回写落盘"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "tym_setting_sim_setup(dev, 0, 16 * 1024, 4096);\n"
            "SettingSrv_Init();\n"
            "SettingSrv_Set(eSettingId_xxx, &val, sizeof(val));\n"
            "SettingSrv_Flush();   /* 或等待 idle 延时回写 */"
        ),
        "hal_map": "tym_setting_sim_port.c 实现 NvmDrv_Ctor 的读/写/擦回调，桥接到统一契约接口。"
                   "移植 = 保留 vendor/ 零修改，重写 port/ 层对接到目标驱动，并按 config/setting_id.h"
                   "调整 ID 表。",
        "cfg_items": [
            ("capacity", "Setting 区容量（字节）", "16384"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "无磨损均衡 / 无 GC / 掉电安全性弱（整页回写非原子）",
            "写放大极大（每次回写整页）",
            "ID 与 Flash 地址的静态映射在 config/setting_id.h 中定义",
        ],
    },
    # ---- fast_flashdb_table（轻量表存储组件）----
    "fastflash": {
        "lib_name": "fastflash",
        "title": "fast_flashdb_table (轻量表存储)",
        "kind": KIND_VENDOR,
        "desc": "轻量表存储组件：建表/按索引读写/追加/删除/GC/掉电重放。",
        "media": "NOR",
        "features": [
            "建表 + 按索引读写 + 追加 + 删除",
            "垃圾回收 + 掉电重放",
            "flash_ops_t 移植接口（简单）",
        ],
        "src_dir": "frameworks/fastflash",
        "copy_files": [
            "frameworks/fastflash/fastflash_sim_port.c",
            "frameworks/fastflash/fastflash_sim_port.h",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_core.c",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_core.h",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_log.c",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_log.h",
            "frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_types.h",
        ],
        "test_entry": "frameworks/fastflash/test/main_fastflash.c",
        "requires": "flash_sim",
        "api": [
            ("fast_flash_sim_init_device(bin_path)", "打开介质（按环境变量配置）"),
            ("fast_flash_init(&flash_ops, base, size)", "初始化并建表"),
            ("fast_flash_write_index / fast_flash_read_index", "按索引读写"),
            ("fast_flash_append / fast_flash_get_count", "追加 / 计数"),
            ("fast_flash_delete_table / 垃圾回收", "删除表 / 回收"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "fast_flash_sim_init_device(\"fastflash.bin\");\n"
            "fast_flash_init(&sim_flash_ops, 0, 16 * 1024);\n"
            "fast_flash_write_index(0, \"hello\", 5);\n"
            "char buf[32]; fast_flash_read_index(0, buf, &len);"
        ),
        "hal_map": "fastflash_sim_port.c 实现组件的 flash_ops_t（init/read/write/erase 四个回调），"
                   "桥接到统一契约接口。移植 = 保留 vendor/ 零修改，重写 port/ 层对接到目标驱动。",
        "cfg_items": [
            ("capacity", "存储区容量（字节）", "16384"),
            ("erase_size", "擦除块大小（字节）", "4096"),
        ],
        "caveats": [
            "fast_flash_log.c 的日志输出对接方式见源码（默认 stdout）",
            "表项/索引上限见 fast_flash_types.h",
        ],
    },
    # ---- Airoha NVDM（KV/裸机持久化，厂商专有）----
    "nvdm": {
        "lib_name": "nvdm",
        "title": "Airoha NVDM (KV/裸机持久化)",
        "kind": KIND_VENDOR,
        "desc": "Airoha NVDM 键值存储组件：PEB 磨损均衡、掉电保护状态机、数据项校验和与垃圾回收。",
        "media": "NOR",
        "features": [
            "group+item 两级命名 KV 存储",
            "PEB 分区磨损均衡",
            "掉电保护状态机 + 数据项校验和",
            "垃圾回收 / 空间与条目查询",
        ],
        "src_dir": "frameworks/nvdm",
        "copy_files": [
            "frameworks/nvdm/nvdm_sim_port.c",
            "frameworks/nvdm/nvdm_sim_port.h",
            "frameworks/nvdm/vendor/src/nvdm_main.c",
            "frameworks/nvdm/vendor/src/nvdm_data.c",
            "frameworks/nvdm/vendor/src/nvdm_io.c",
            "frameworks/nvdm/vendor/inc/nvdm.h",
            "frameworks/nvdm/vendor/inc/nvdm_port.h",
            "frameworks/nvdm/vendor/inc/nvdm_internal.h",
            "frameworks/nvdm/vendor/inc/nvdm_msgid_log.h",
        ],
        "test_entry": "frameworks/nvdm/test/main_nvdm.c",
        "cflags": ["-DMTK_NVDM_ENABLE"],
        "requires": "flash_sim",
        "api": [
            ("nvdm_sim_setup(dev, base, capacity, peb_size, item_count)", "注入分区配置（须在 nvdm_init 前）"),
            ("nvdm_init()", "初始化（仅一次）"),
            ("nvdm_write_data_item(group, item, type, buf, size)", "写/更新"),
            ("nvdm_read_data_item(group, item, buf, &size)", "读（size 出入参）"),
            ("nvdm_delete_data_item / nvdm_delete_group / nvdm_delete_all", "删除"),
            ("nvdm_query_space_information / nvdm_trigger_garbage_collection", "空间查询 / 主动 GC"),
        ],
        "use_hint": (
            "flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "nvdm_sim_setup(dev, 0, 16 * 1024, 4096, 200);\n"
            "nvdm_init();\n"
            "uint8_t buf[] = \"hello\";\n"
            "nvdm_write_data_item(\"app\", \"key\", NVDM_DATA_ITEM_TYPE_RAW_DATA, buf, 5);\n"
            "uint8_t rb[64]; uint32_t rl = sizeof(rb);\n"
            "nvdm_read_data_item(\"app\", \"key\", rb, &rl);"
        ),
        "hal_map": "nvdm_sim_port.c 实现 nvdm_port.h 声明的 ~20 个契约函数（flash 读写擦 / 分区 / "
                   "malloc / 互斥 / 日志等），桥接到统一契约接口。移植 = 保留 vendor/ 零修改，"
                   "按 PORTING.md 重写 port/ 层对接到目标驱动与 OS。",
        "cfg_items": [
            ("capacity", "NVDM 分区容量（字节，>= 2*peb_size）", "16384"),
            ("peb_size", "物理擦除块大小（= 介质 erase_size）", "4096"),
            ("item_count", "最大数据项数量", "200"),
        ],
        "caveats": [
            "nvdm_init 仅允许一次；进程内重启需 nvdm_sim_reset()",
            "单条数据项上限 1024 字节（可配，最大 2048）",
            "组名 <=15、项名 <=31 字符（含 '\\0'）",
            "依赖块擦除语义，EEPROM 不适用",
            "编译需 -DMTK_NVDM_ENABLE",
        ],
    },
}


# ---------------------------------------------------------------------------
# 文件归类：把 ROOT 相对路径映射到包内 (目录, 相对路径)
# ---------------------------------------------------------------------------
def _classify(fn, kind):
    """返回 (dest_dir, arc_rel)。

    - zephyr：zephyr_compat/include/zephyr/** 保留为 port/zephyr/**（供
      #include <zephyr/...> 解析）；vendor/include/zephyr/** 保留为 vendor/zephyr/**。
    - vendor：/vendor/ 下源码进 vendor/；移植层（sim_port/ef_port/fal_*/compat/）
      进 port/；配置文件进 config/；其余公共头进 include/。
    - self：全部进 core/。
    """
    b = os.path.basename(fn)
    if kind == KIND_ZEPHYR:
        if "/zephyr_compat/include/" in fn:
            return DIR_PORT, fn.split("/zephyr_compat/include/", 1)[1]
        if "/zephyr_compat/" in fn:
            return DIR_PORT, b
        if "/vendor/include/" in fn:
            return DIR_VENDOR, fn.split("/vendor/include/", 1)[1]
        if "/vendor/" in fn:
            return DIR_VENDOR, b
        return DIR_PORT, b
    if kind == KIND_VENDOR:
        if "/vendor/" in fn:
            return DIR_VENDOR, b
        if ("sim_port" in b or b.startswith("ef_port")
                or "fal_flash_sim_port" in b or "host_types" in b
                or "/compat/" in fn):
            return DIR_PORT, b
        if "/config/" in fn or _CONFIG_RE.search(b) or b.endswith(".config"):
            return DIR_CONFIG, b
        return DIR_INCLUDE, b
    return DIR_CORE, b


def _map_cflags(recipe, kind):
    """把 recipe.cflags 中的 -include 相对路径改写为包内相对路径。"""
    out = []
    cfl = recipe.get("cflags", [])
    i = 0
    while i < len(cfl):
        a = cfl[i]
        if a == "-include" and i + 1 < len(cfl):
            d, rel = _classify(cfl[i + 1], kind)
            out += ["-include", "%s/%s" % (d, rel)]
            i += 2
        else:
            out.append(a)
            i += 1
    return out


def _collect_files(recipe, kind):
    """收集包内全部源码文件：recipe.copy_files + 统一 HAL 参考实现/契约头。

    返回 [(dest_dir, arc_rel, full_path), ...]（已去重）。
    """
    files = []
    for fn in recipe["copy_files"]:
        full = os.path.join(ROOT, fn)
        if not os.path.exists(full):
            full = os.path.join(ROOT, recipe["src_dir"], fn)
        if not os.path.exists(full):
            raise RuntimeError("源文件缺失: %s" % full)
        d, rel = _classify(fn, kind)
        files.append((d, rel, full))
    # 统一 HAL 契约：core/flash_sim.c 为 PC 参考实现，include/flash_sim.h 为契约头。
    # 二者不进 lib_sources（平台/目标侧提供真实实现），仅作参考与演示编译用。
    files.append((DIR_CORE, "flash_sim.c",
                  os.path.join(ROOT, "simulator", "flash_sim.c")))
    files.append((DIR_INCLUDE, "flash_sim.h",
                  os.path.join(ROOT, "simulator", "flash_sim.h")))
    seen = set()
    uniq = []
    for d, rel, full in files:
        key = (d, rel)
        if key in seen:
            continue
        seen.add(key)
        uniq.append((d, rel, full))
    return uniq


# ---------------------------------------------------------------------------
# 文档渲染
# ---------------------------------------------------------------------------
def _api_table(recipe):
    rows = []
    for sig, desc in recipe.get("api", []):
        rows.append("| `%s` | %s |" % (sig, desc))
    return "\n".join(rows) if rows else "| （见 demo/test_main.c） | |"


def _cfg_table(recipe):
    rows = ["| 参数 | 含义 | 默认值 | 目标平台调整 |",
            "|------|------|--------|--------------|"]
    for name, desc, default in recipe.get("cfg_items", []):
        rows.append("| %s | %s | %s | 按目标 flash_map / 分区表调整 |" % (name, desc, default))
    return "\n".join(rows)


def _feature_bullets(recipe):
    return "\n".join("- %s" % f for f in recipe.get("features", []))


def _render_readme(fw_id, recipe, params, pkg):
    lib = recipe["lib_name"]
    title = recipe["title"]
    api_sigs = "、".join("`%s`" % sig.split("(")[0]
                        for sig, _ in recipe.get("api", []))
    return (
        "# %s（可移植库包）\n\n"
        "%s\n\n"
        "> 本包由 Flash 存储仿真平台「代码生成引擎」导出，已在模拟基座上验证通过，"
        "可直接移植到嵌入式目标。\n\n"
        "## 快速上手（PC 冒烟验证）\n\n"
        "```bash\n"
        "# 解压后进入包目录执行（命令见 demo/BUILD.md）\n"
        "%s\n"
        "```\n\n"
        "## 对外接口\n\n"
        "%s\n\n"
        "## 目录结构（层次说明）\n\n"
        "```\n"
        "%s/\n"
        "├── core/       框架核心（平台无关）或 HAL 参考实现 core/flash_sim.c\n"
        "├── vendor/     开源/厂商源码（零修改，只读，不要改动）\n"
        "├── port/       平台移植层（目标平台替换/重写点）\n"
        "├── config/     配置文件模板（分区/几何参数）\n"
        "├── include/    对外公共头 + HAL 契约头 include/flash_sim.h\n"
        "└── demo/       自检入口 test_main.c + 构建说明 BUILD.md\n"
        "```\n\n"
        "## 文档索引\n\n"
        "- `PORTING.md` — 完整移植文档（API / HAL 契约 / 配置 / 编译 / 集成 / 限制 / 验证）\n"
        "- `HAL_CONTRACT.md` — 统一适配接口契约（所有框架一致的 Flash 操作抽象）\n"
        "- `AI_PORTING_PROMPT.md` — 给目标工程 AI 的移植提示词（可直接投喂）\n"
        "- `demo/BUILD.md` — 构建与运行命令\n"
        "- `manifest.json` — 包元信息\n"
    ) % (title, recipe["desc"], pkg["build_cmd"], api_sigs, pkg["top"])


def _render_hal_contract(recipe, pkg):
    title = recipe["title"]
    hal_map = recipe.get("hal_map", "见 `port/` 目录下的移植层源码。")
    return (
        "# 统一适配接口契约（HAL Contract）\n\n"
        "## 1. 为什么有一份统一契约\n\n"
        "本平台所有存储框架（KV / 文件系统 / 裸机配置 / 环形缓冲）都通过 **同一套最小 "
        "Flash 操作抽象** 访问介质，即 `include/flash_sim.h` 声明的接口。"
        "因此**移植到任何目标平台，本质只有一件事**：\n\n"
        "> 按本契约实现 5 个 Flash 操作函数，对接目标 MCU 的真实 Flash 驱动。\n\n"
        "## 2. 契约接口（include/flash_sim.h）\n\n"
        "| 函数 | 语义 | 备注 |\n"
        "|------|------|------|\n"
        "| `flash_sim_init(cfg)` | 打开/初始化介质，返回句柄；失败返回 NULL | cfg 含几何/寿命/性能参数 |\n"
        "| `flash_sim_read(dev, off, buf, len)` | 从 off 随机读 len 字节 | 任意粒度 |\n"
        "| `flash_sim_write(dev, off, buf, len)` | 向 off 写 len 字节 | NOR/NAND 前须已擦除（仅 1->0） |\n"
        "| `flash_sim_erase(dev, off, len)` | 按擦除块大小整块擦除 | 仅 NOR/NAND |\n"
        "| `flash_sim_deinit(dev)` | 关闭介质 | |\n\n"
        "配套类型/宏：`flash_type_t`、`flash_config_t`、`flash_err_t`、"
        "`FLASH_CFG_DEFAULTS_BY_TYPE`；统计/磨损接口（`flash_sim_get_stats` / "
        "`flash_sim_get_wear_map` / `flash_sim_block_count`）为非移植必需的可选接口。\n\n"
        "## 3. 契约语义要点（移植时务必满足）\n\n"
        "- **写前擦除**：NOR/NAND 写入仅允许 1->0，写前区域必须已擦除（全 0xFF）。\n"
        "- **擦除粒度**：必须按 `erase_size` 对齐整块擦除；框架按此规划分区。\n"
        "- **对齐**：分区基址/容量须为 `erase_size` 整数倍。\n"
        "- **返回码**：0 成功；负值错误码（见 `flash_err_t`：ARGS/RANGE/ERASE/WRITE/NOTSUP/IO）。\n"
        "- **参考实现**：`core/flash_sim.c` 是本契约在 PC 上的参考实现（BIN 文件仿真），"
        "移植时把同样的语义映射到真实驱动即可。\n\n"
        "## 4. 本框架（%s）的接口映射\n\n"
        "%s\n\n"
        "移植层源码见 `port/` 目录；`vendor/` 为框架本体（零修改，只读）。\n\n"
        "## 5. 移植建议\n\n"
        "- 目标 Flash 驱动通常是 读(addr, buf, len) / 写(addr, buf, len) / 擦除(addr, len)，"
        "在 `port/` 移植层做一层适配即可。\n"
        "- 若目标平台已有成熟 Flash 抽象（如 RT-Thread FAL、Zephyr flash API、裸机 SPI 驱动），"
        "把契约函数桥接到对应 API。\n"
        "- 不要修改 `vendor/`；`core/` 中除 `flash_sim.c` 外的框架核心代码也不要改。\n"
        "- 让 `AI_PORTING_PROMPT.md` 指引目标工程里的 AI 完成剩余适配。\n"
    ) % (title, hal_map)


def _render_porting(fw_id, recipe, params, pkg):
    lib = recipe["lib_name"]
    title = recipe["title"]
    kind = recipe.get("kind", KIND_SELF)
    kind_name = {KIND_SELF: "自研框架（直接使用统一契约）",
                 KIND_VENDOR: "开源/厂商组件（vendor + 移植层）",
                 KIND_ZEPHYR: "Zephyr 组件（vendor + 兼容层）"}[kind]
    files = "\n".join("- `%s/%s`" % (d, r)
                      for d, r, _ in pkg["files"])
    c_sources = "\n".join("- `%s`" % s for s in pkg["c_sources"])
    incs = ", ".join("-I%s" % d for d in PACKAGE_DIRS)
    cflags = " ".join(pkg["cflags"]) if pkg["cflags"] else "（无）"
    use_hint = recipe.get("use_hint", "")
    media = recipe.get("media", "NOR")
    vendor_count = sum(1 for d, _, _ in pkg["files"] if d == DIR_VENDOR)

    api_map = {
        KIND_SELF: "本框架直接调用统一契约接口（`flash_sim_read/write/erase`）。"
                   "目标平台只需按 `include/flash_sim.h` 实现真实驱动。",
        KIND_VENDOR: "本框架通过 `port/` 下的移植层访问统一契约接口："
                     "vendor 源码零修改，移植层将框架原生底层接口（回调/diskio/ops）"
                     "桥接到 `flash_sim_*`。详见 `HAL_CONTRACT.md`。",
        KIND_ZEPHYR: "本框架 vendor 使用 Zephyr 风格 API（`<zephyr/...>`），"
                     "`port/` 下的 zephyr_compat 兼容层将其桥接到统一契约接口。"
                     "vendor 源码零修改。详见 `HAL_CONTRACT.md`。",
    }[kind]

    cfg_text = (
        "### 分区与几何参数\n\n"
        "%s\n\n"
        "> 分区基址与容量须按目标工程的 flash_map / 链接脚本调整；"
        "本平台 demo 默认从偏移 0 开始。\n"
    ) % _cfg_table(recipe)

    return (
        "# %s 移植说明（PORTING）\n\n"
        "## 1. 组件概述\n\n"
        "- 名称：%s\n"
        "- 类别：%s\n"
        "- 适用介质：%s\n"
        "- 特性：\n%s\n"
        "- 代码规模：核心/移植层文件 %d 个（其中 vendor 零修改 %d 个）\n\n"
        "## 2. 包结构（层次说明）\n\n"
        "```\n"
        "%s/\n"
        "├── core/       框架核心（平台无关）或 HAL 参考实现 core/flash_sim.c\n"
        "├── vendor/     开源/厂商源码（零修改，只读）\n"
        "├── port/       平台移植层（目标平台替换/重写点）\n"
        "├── config/     配置文件模板（分区/几何参数）\n"
        "├── include/    对外公共头 + HAL 契约头 include/flash_sim.h\n"
        "└── demo/       自检入口 test_main.c + 构建说明 BUILD.md\n"
        "```\n\n"
        "### 文件清单\n\n"
        "%s\n\n"
        "## 3. 对外 API\n\n"
        "%s\n\n"
        "### 最小使用示例\n\n"
        "```c\n"
        "%s\n"
        "```\n\n"
        "## 4. 平台依赖接口（HAL 契约）\n\n"
        "%s\n\n"
        "> 完整接口规格与映射表见 `HAL_CONTRACT.md`。\n\n"
        "## 5. 配置参数\n\n"
        "%s\n"
        "> 本次导出参数：`%s`\n\n"
        "## 6. 条件编译开关\n\n"
        "```\n%s\n```\n\n"
        "## 7. OS/库依赖\n\n"
        "- 内存：%s\n"
        "- 锁/多线程：本平台仿真为单线程；多线程环境需自行加锁（见 port/ 层注释）。\n"
        "- 时间：无强依赖（本平台仿真通过 flash_sim 注入耗时）。\n"
        "- 日志：port/ 层默认输出到 stdout；目标平台替换为你的日志接口。\n"
        "- C 标准：C99（`-std=c99`）。\n\n"
        "## 8. 编译与集成\n\n"
        "### 需要编译的源文件（除 demo 外）\n\n"
        "%s\n\n"
        "### 包含目录\n\n"
        "```\n%s\n```\n\n"
        "### 必须定义的宏 / 强制包含头\n\n"
        "```\n%s\n```\n\n"
        "### 初始化顺序\n\n"
        "1. 实现/替换 `port/` 移植层（或按契约提供真实驱动）；\n"
        "2. 按第 5 节配置分区与几何参数；\n"
        "3. 调用框架初始化（见第 3 节示例）；\n"
        "4. 掉电重启后重新执行初始化（框架自身在 init 时扫描恢复）。\n\n"
        "## 9. 已知限制与坑\n\n"
        "%s\n\n"
        "## 10. 验证方法\n\n"
        "在 PC 上按 `demo/BUILD.md` 编译运行 demo 可先验证框架行为；"
        "移植到目标后再用 `demo/test_main.c` 的用例做冒烟测试（写/读/删/掉电）。\n\n"
        "## 11. AI 辅助移植\n\n"
        "将本包目录与 `AI_PORTING_PROMPT.md` 一起交给目标工程里的 AI，"
        "由其完成驱动对接、配置调整与构建接入。\n"
    ) % (
        title, title, kind_name, media, _feature_bullets(recipe),
        len(pkg["files"]), vendor_count,
        pkg["top"], files,
        _api_table(recipe), use_hint,
        api_map, cfg_text,
        json.dumps(params, ensure_ascii=False),
        cflags,
        "需要 malloc/free：%s" % ("是（见 HAL_CONTRACT.md 第 7 节）"
                                  if kind != KIND_SELF else "否（自研框架可用静态内存）"),
        c_sources, incs, cflags,
        "\n".join("- %s" % c for c in recipe.get("caveats", [])),
    )


def _render_ai_prompt(fw_id, recipe, params, pkg):
    title = recipe["title"]
    lib = recipe["lib_name"]
    return (
        "# AI 移植适配提示词（投喂给目标工程里的 AI）\n\n"
        "> 使用方式：把 **本文件** 与 **整个解压目录**（%s/）一起交给目标工程里的 AI。\n"
        "> 该 AI 能看到：目标工程全部代码 + 本包全部文件。它看不到本平台的其它实现。\n"
        "> 本文件是可独立使用的最小提示词，也可以让 AI 先通读 `PORTING.md` 与 `HAL_CONTRACT.md` 再动手。\n\n"
        "---\n\n"
        "## 你的角色与任务\n\n"
        "你是嵌入式存储组件移植工程师。目标：把 `%s`（%s）从「可移植库包」状态"
        "移植到本工程并接入使用，使其能在目标硬件上稳定运行。\n\n"
        "## 输入\n\n"
        "1. **本库包**（当前目录）：已按 `core/vendor/port/config/include/demo` 分层组织：\n"
        "   - `core/`：框架核心（平台无关）或 HAL 参考实现 `core/flash_sim.c`（PC 仿真，仅供对照语义）。\n"
        "   - `vendor/`：框架本体源码（**零修改，只读**，不要改它的任何注释/格式/逻辑）。\n"
        "   - `port/`：平台移植层（**你的主要工作点**）。\n"
        "   - `config/`：配置文件模板（分区/几何参数）。\n"
        "   - `include/`：对外公共头 + HAL 契约头 `include/flash_sim.h`。\n"
        "   - `demo/`：`test_main.c` 自检用例 + `BUILD.md` 构建说明。\n"
        "   - `PORTING.md`、`HAL_CONTRACT.md`：完整规格。\n"
        "2. **目标工程上下文**（由你自行在本工程中查找）：\n"
        "   - Flash 驱动：SPI/QSPI/NAND 控制器的 read/write/erase 接口；\n"
        "   - 分区/内存布局：flash_map、链接脚本、可用的存储分区（基址+容量+擦除块大小）；\n"
        "   - 构建系统：编译参数（-I / 源文件清单 / -D）、C 标准；\n"
        "   - OS/基础库：malloc、互斥锁、时间戳、日志接口（若有）。\n\n"
        "## 移植步骤\n\n"
        "1. **读规格**：通读 `PORTING.md` 与 `HAL_CONTRACT.md`，确认对外 API 与 HAL 契约。\n"
        "2. **实现 HAL 或重写移植层**：\n"
        "   - 若本包是自研框架（直接使用契约）：按 `include/flash_sim.h` 实现 5 个函数"
        "（init/read/write/erase/deinit），对接目标 Flash 驱动；\n"
        "   - 若本包带移植层（`port/`）：把移植层对 `flash_sim_*` 的调用改写为目标驱动调用，"
        "或直接按契约实现 `flash_sim_*` 后复用移植层。\n"
        "3. **配置参数**：按 `PORTING.md` 第 5 节与目标 flash_map，调整 `config/` 中的分区/几何"
        "参数（基址、容量、擦除块大小、条目数上限等）。\n"
        "4. **接入构建**：把 `PORTING.md` 第 8 节的源文件清单、包含目录、-D 宏加入目标构建系统；"
        "注意 `-include` 的强制头（若有）。\n"
        "5. **验证**：先按 `demo/BUILD.md` 在 PC 环境跑通 demo 确认框架行为；再在目标硬件上移植后"
        "用 `demo/test_main.c` 的用例做写/读/删/掉电冒烟测试。\n"
        "6. **收尾**：把实际改动与最终分区取值回填到本目录 `PORTING.md`（第 5/8 节），便于后续维护。\n\n"
        "## 硬性约束\n\n"
        "- `vendor/` **零修改**：禁止改动其任何内容；需要调整行为时改 `port/` 层或 `config/`。\n"
        "- `core/` 中除 `flash_sim.c`（参考实现，可替换）外的框架核心代码不要改。\n"
        "- 遵循目标工程的代码风格与既有抽象；优先复用目标工程已有的驱动封装。\n"
        "- 内存安全：检查边界、释放后置 NULL、不越界；按目标平台规范处理。\n"
        "- 所有返回码必须检查；不得吞掉错误。\n"
        "- 改动必须可编译、可运行、可回滚；不确定的改动要注释说明并保留原逻辑。\n\n"
        "## 交付物\n\n"
        "完成后请汇报：\n"
        "- 移植层/配置/构建接入的具体改动（文件 + 关键片段）；\n"
        "- 按目标工程调整的参数取值（分区基址/容量/擦除块/条目数等）；\n"
        "- 验证方式与结果（编译/运行输出摘要）；\n"
        "- 遗留风险与后续建议。\n"
    ) % (pkg["top"], lib, title)


def _build_cmd_line(recipe, pkg):
    """生成可一键执行的 gcc 构建命令（PC 冒烟：HAL 参考实现 + 框架源码 + demo）。"""
    srcs = [os.path.join(".", s) for s in pkg["build_sources"]]
    inc_flags = " ".join("-I%s" % d for d in PACKAGE_DIRS)
    cflags = " ".join(pkg["cflags"])
    return " ".join(["gcc -std=c99 -Wall -Wextra", inc_flags, cflags,
                     "-o demo_test", "demo/test_main.c"] + srcs)


def _render_build_md(recipe, pkg):
    """生成 demo/BUILD.md：可一键执行的构建命令。"""
    cmd = _build_cmd_line(recipe, pkg)
    return (
        "# 构建与运行（demo 冒烟验证）\n\n"
        "在**本包根目录**执行（依赖本包自带的 `core/flash_sim.c` 参考实现，无需外部库）：\n\n"
        "```bash\n"
        "%s\n"
        "./demo_test\n"
        "```\n\n"
        "说明：\n"
        "- `core/flash_sim.c` 是统一契约的 PC 参考实现（BIN 文件仿真）；\n"
        "- 移植到真实硬件后，替换 `core/flash_sim.c` 为你的真实驱动实现（或改写 `port/` 层），"
        "demo 与框架逻辑不变；\n"
        "- 各框架测试项/参数见 `test_main.c` 头部注释（环境变量驱动，PC 下可省略）。\n"
    ) % cmd


def _render_test_main(fw_id, recipe, params):
    """生成参数化的标准自检入口 demo/test_main.c（用占位符 replace，避免 % 冲突）。"""
    capacity = int(params.get("capacity", 8192))
    page_size = int(params.get("page_size", 256))
    erase_size = int(params.get("erase_size", 4096))
    lib = recipe["lib_name"]
    title = recipe["title"]
    if fw_id == "kv":
        tpl = (
            "/* 自动生成的自检入口：对接模拟基座验证 __TITLE__ 库 */\n"
            "#include \"flash_sim.h\"\n"
            "#include \"__LIB__.h\"\n"
            "#include <stdio.h>\n"
            "#include <string.h>\n\n"
            "#define KV_CAPACITY __CAPACITY__\n"
            "#define KV_ERASE_SIZE __ERASE_SIZE__\n"
            "#define BIN_PATH \"imported_kv_demo.bin\"\n\n"
            "static int g_fail = 0;\n"
            "static void expect(const char *name, int cond) {\n"
            "    printf(\"  [%s] %s\\n\", cond ? \"OK  \" : \"FAIL\", name);\n"
            "    if (!cond) g_fail++;\n"
            "}\n\n"
            "int main(void) {\n"
            "    printf(\"=== 导入库运行验证: __TITLE__ (cap=__CAPACITY__) ===\\n\");\n"
            "    flash_config_t cfg = {\n"
            "        .type = FLASH_TYPE_NOR, .total_size = 64*1024,\n"
            "        .erase_size = KV_ERASE_SIZE, .write_size = 1, .read_size = 1,\n"
            "        .erase_cycles = 100000, .bin_path = BIN_PATH };\n"
            "    flash_dev_t *dev = flash_sim_init(&cfg);\n"
            "    if (!dev) { printf(\"flash init failed\\n\"); return 1; }\n"
            "    flash_sim_erase(dev, 0, KV_CAPACITY);\n"
            "    kv_init(dev, 0, KV_CAPACITY);\n"
            "    const char *v = \"hello-imported\";\n"
            "    expect(\"kv_write\", kv_write(dev, 1, v, (uint16_t)strlen(v)) == FLASH_OK);\n"
            "    char rb[32] = {0}; uint16_t rl = sizeof(rb);\n"
            "    expect(\"kv_read\", kv_read(dev, 1, rb, &rl) == FLASH_OK);\n"
            "    expect(\"kv_data_match\", rl == strlen(v) && memcmp(rb, v, rl) == 0);\n"
            "    int32_t n = 0xCAFEBABE; uint16_t rn = sizeof(n);\n"
            "    expect(\"kv_write_int\", kv_write(dev, 2, &n, sizeof(n)) == FLASH_OK);\n"
            "    int32_t rn2 = 0; uint16_t rnl = sizeof(rn2);\n"
            "    expect(\"kv_read_int\", kv_read(dev, 2, &rn2, &rnl) == FLASH_OK);\n"
            "    expect(\"kv_int_match\", rn2 == n);\n"
            "    expect(\"kv_delete\", kv_delete(dev, 1) == FLASH_OK);\n"
            "    uint16_t dl = 4;\n"
            "    expect(\"kv_read_deleted\", kv_read(dev, 1, NULL, &dl) == FLASH_ERR_ARGS);\n"
            "    flash_sim_deinit(dev);\n"
            "    printf(\"\\n=== 导入库验证结果: %s ===\\n\", g_fail == 0 ? \"全部通过\" : \"存在失败\");\n"
            "    return g_fail == 0 ? 0 : 1;\n"
            "}\n"
        )
        return (tpl
                .replace("__TITLE__", title)
                .replace("__LIB__", lib)
                .replace("__CAPACITY__", str(capacity))
                .replace("__ERASE_SIZE__", str(erase_size)))
    # simulator 自检入口
    tpl = (
        "/* 自动生成的自检入口：验证 flash_sim 基座库 */\n"
        "#include \"flash_sim.h\"\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n\n"
        "#define BIN_PATH \"imported_sim_demo.bin\"\n\n"
        "static int g_fail = 0;\n"
        "static void expect(const char *name, int cond) {\n"
        "    printf(\"  [%s] %s\\n\", cond ? \"OK  \" : \"FAIL\", name);\n"
        "    if (!cond) g_fail++;\n"
        "}\n\n"
        "int main(void) {\n"
        "    printf(\"=== 导入库运行验证: __TITLE__ ===\\n\");\n"
        "    flash_config_t cfg = {\n"
        "        .type = FLASH_TYPE_NOR, .total_size = 64*1024,\n"
        "        .erase_size = __ERASE_SIZE__, .write_size = 1, .read_size = 1,\n"
        "        .erase_cycles = 100000, .bin_path = BIN_PATH };\n"
        "    flash_dev_t *dev = flash_sim_init(&cfg);\n"
        "    if (!dev) { printf(\"init failed\\n\"); return 1; }\n"
        "    uint8_t w[16]; for (int i=0;i<16;i++) w[i]=(uint8_t)(0xA0+i);\n"
        "    uint8_t r[16]={0};\n"
        "    expect(\"erase\", flash_sim_erase(dev,0,cfg.erase_size)==FLASH_OK);\n"
        "    expect(\"write\", flash_sim_write(dev,0,w,16)==FLASH_OK);\n"
        "    expect(\"read\", flash_sim_read(dev,0,r,16)==FLASH_OK);\n"
        "    expect(\"match\", memcmp(w,r,16)==0);\n"
        "    flash_sim_deinit(dev);\n"
        "    printf(\"\\n=== 导入库验证结果: %s ===\\n\", g_fail==0?\"全部通过\":\"存在失败\");\n"
        "    return g_fail==0?0:1;\n"
        "}\n"
    )
    return tpl.replace("__TITLE__", title).replace("__ERASE_SIZE__", str(erase_size))


# ---------------------------------------------------------------------------
# 主入口
# ---------------------------------------------------------------------------
def generate_zip(fw_id, params):
    """生成库文件 zip 包，返回 (zip_bytes, manifest_dict)。

    失败时抛出 RuntimeError，message 描述原因。
    """
    recipe = RECIPES.get(fw_id)
    if not recipe:
        raise RuntimeError("不支持生成的框架: %s" % fw_id)

    lib = recipe["lib_name"]
    kind = recipe.get("kind", KIND_SELF)
    ts = int(time.time())
    top = "%s_library" % lib

    files = _collect_files(recipe, kind)
    # 包内所有 .c（含 HAL 参考实现 flash_sim.c，供 PC demo 编译；不含 test_main.c）
    all_c = ["%s/%s" % (d, r) for d, r, _ in files if r.endswith(".c")]
    # lib_sources：导入到平台闭环编译用。HAL 参考实现 flash_sim.c 由平台自带，
    # 从 lib_sources 剔除，避免重复定义。
    lib_sources = [p for p in all_c if p != "core/flash_sim.c"]
    sources = ["%s/%s" % (d, r) for d, r, _ in files if r.endswith((".c", ".h"))]

    cflags = _map_cflags(recipe, kind)
    manifest = {
        "id": "%s_export_%d" % (fw_id, ts),
        "name": "%s（导出库）" % recipe["title"],
        "desc": recipe["desc"],
        "base": fw_id,
        "requires": recipe.get("requires", "flash_sim"),
        "cflags": cflags,
        "includes": list(PACKAGE_DIRS),
        "params": params,
        "entry": "demo/test_main.c",
        "lib": lib,
        "lib_sources": lib_sources,
        "sources": sources + ["demo/test_main.c"],
    }

    # 测试入口：基础框架（kv/simulator）自动生成，其余保留框架自带 main
    test_entry = recipe.get("test_entry")
    if test_entry:
        full = os.path.join(ROOT, test_entry)
        if not os.path.exists(full):
            raise RuntimeError("测试入口缺失: %s" % full)
        test_main = open(full, "rb").read()
    else:
        test_main = _render_test_main(fw_id, recipe, params).encode("utf-8")

    # demo 构建命令（PC 冒烟）：flash_sim.c 参考实现 + 框架源码 + test_main
    # （test_main 由 _build_cmd_line 单独追加，build_sources 只放框架源码）
    build_sources = all_c
    pkg = {
        "files": files,
        "c_sources": lib_sources,
        "build_sources": build_sources,
        "cflags": cflags,
        "top": top,
    }
    pkg["build_cmd"] = _build_cmd_line(recipe, pkg)

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for d, rel, full in files:
            arc = "%s/%s/%s" % (top, d, rel)
            zf.writestr(arc, open(full, "rb").read())
        zf.writestr("%s/demo/test_main.c" % top, test_main)
        zf.writestr("%s/demo/BUILD.md" % top,
                    _render_build_md(recipe, pkg))
        zf.writestr("%s/README.md" % top, _render_readme(fw_id, recipe, params, pkg))
        zf.writestr("%s/PORTING.md" % top,
                    _render_porting(fw_id, recipe, params, pkg))
        zf.writestr("%s/HAL_CONTRACT.md" % top,
                    _render_hal_contract(recipe, pkg))
        zf.writestr("%s/AI_PORTING_PROMPT.md" % top,
                    _render_ai_prompt(fw_id, recipe, params, pkg))
        zf.writestr("%s/manifest.json" % top,
                    json.dumps(manifest, ensure_ascii=False, indent=2))
    return buf.getvalue(), manifest
