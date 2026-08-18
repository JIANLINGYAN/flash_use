# Flash 存储仿真平台

嵌入式 Flash 存储管理设计与仿真平台。采用**三层架构**：

1. **驱动层（模拟基座）**：`simulator/flash_sim` —— NOR/NAND/EEPROM 物理
   特性仿真，独立性能检测（读写擦次数/耗时/磨损统计）与参数配置。
2. **组件层**：`frameworks/*` —— 各类适配了模拟基座的存储组件，分为
   **裸机简单框架 / KV 管理 / 文件系统** 三类。组件本体（尤其开源组件
   vendor）零修改，只新增移植层（sim_port）。
3. **应用层测试框架**：`app/*` —— 统一任务引擎，通过**适配层**
   （`app/adapter/*`）调用组件层，对不同任务执行对应测试选项，并独立
   做**性能计算**（吞吐 / 写放大 / 介质阻塞 / 磨损分布 / 掉电安全）。

并提供 **前端模拟运行界面** 进行可视化验证（左侧框架按分类分组展示，
含独立"应用层测试"标签页做跨组件横向对比）。

## 项目结构

```
flash_use/
├── simulator/            # 模块一：模拟基座（C 库）
│   ├── flash_sim.h/.c    #   统一 Flash 接口 read/write/erase，BIN 落盘
│   └── test/main_sim.c   #   自检：NOR/NAND/EEPROM 物理特性
├── app/                  # 应用层测试框架（统一任务引擎 + 适配层）
│   ├── app_common.h      #   测试选项/任务/结果统计/组件适配器接口定义
│   ├── app_register.c/.h #   适配器自注册表（构造函数注册）
│   ├── app_util.c/.h     #   环境变量/计时/STATS_JSON 输出
│   ├── app_task.c/.h     #   任务引擎：write/read/update/durability/powerloss/mixed
│   ├── adapter/          #   组件适配器（kv_store/easyflash/flashdb/fastflash/
│   │                     #   nvdm/nvs/zms/fcb/tym_setting/baremetal/fs_store/
│   │                     #   littlefs/fatfs/spiffs/yaffs）
│   └── test/main_app.c   #   统一入口（APP_COMPONENT + APP_TASK 选择）
├── frameworks/           # 组件层：存储框架库（对接 simulator，可移植 C）
│   ├── kv/               #   自研 KV/NVS 框架
│   │   ├── kv_store.h/.c #     两步提交掉电安全 + CRC + 压实 GC
│   │   └── test/main_kv.c#     运行验证
│   ├── easyflash/        #   开源 KV 组件（armink/EasyFlash V4.x NG 模式）
│   │   ├── ef_port.c/.h  #     适配层：对接模拟基座
│   │   ├── ef_cfg.h      #     运行时可配置参数
│   │   ├── vendor/       #     upstream 源码（ef_env/ef_utils/easyflash）
│   │   └── test/main_easyflash.c
│   ├── flashdb/          #   开源 KV 组件（armink/FlashDB KVDB + FAL）
│   │   ├── fal_flash_sim_port.c/.h  # FAL 适配层
│   │   ├── fdb_cfg.h/fal_cfg.h
│   │   ├── vendor/       #     upstream 源码（fdb_* + fal_*）
│   │   └── test/main_flashdb.c
│   ├── baremetal/        #   裸机结构体配置框架（A/B 双备份 + CRC32 + 单调序号）
│   │   ├── bm_config.h/.c
│   │   └── test/main_baremetal.c
│   ├── fastflash/        #   开源组件 fast_flashdb_table（轻量表存储）
│   │   ├── fastflash_sim_port.c/.h  # 移植层：对接模拟基座
│   │   ├── vendor/fast_flashdb_table# upstream 源码（core/ + port_win/）
│   │   └── test/main_fastflash.c
│   ├── nvdm/              #   Airoha NVDM（KV/裸机持久化，厂商专有）
│   │   ├── nvdm_sim_port.c/.h      # 移植层：nvdm_port_* 契约对接模拟基座
│   │   ├── vendor/       #     framework 源码（nvdm_main/data/io，零修改）
│   │   └── test/main_nvdm.c
│   ├── zephyr_compat/    #   Zephyr 存储组件共享兼容层（模拟 zephyr 抽象桥接模拟基座）
│   │   ├── zephyr_compat.c/.h      # flash 设备/flash_area/k_mutex/CRC 实现
│   │   └── include/zephyr/         # 模拟 zephyr 头（device/flash/flash_map/...）
│   ├── fcb/               #   开源组件 Zephyr FCB（闪存环形缓冲/KV）
│   │   ├── vendor/       #     upstream 源码（fcb*.c，零修改）
│   │   └── test/main_fcb.c
│   ├── nvs/               #   开源组件 Zephyr NVS（KV/裸机持久化）
│   │   ├── vendor/       #     upstream 源码（nvs.c，零修改）
│   │   └── test/main_nvs.c
│   ├── zms/               #   开源组件 Zephyr ZMS（KV/固定槽位存储）
│   │   ├── vendor/       #     upstream 源码（zms.c，零修改）
│   │   └── test/main_zms.c
│   ├── tym_setting/       #   厂商组件 TYM Setting（ID静态表/RAM镜像，去耦裁剪）
│   │   ├── tym_setting_sim_port.c/.h # 移植层：NvmDrv 回调桥接模拟基座
│   │   ├── vendor/       #     框架源码（去 FreeRTOS/ui_shell/hal_log 耦合）
│   │   ├── config/       #     数据契约三表（eSettingId + settingDB + romMap）
│   │   ├── compat/       #     cplus/commonTypes/日志抽象
│   │   └── test/main_tym_setting.c
│   ├── fs/               #   自研文件系统框架（块分配表 + 数据块）
│   │   ├── fs_store.h/.c #     多文件读写/频繁修改/追加/删除/查询
│   │   └── test/main_fs.c
│   ├── littlefs/         #   开源文件系统（littlefs-project/littlefs v2.x）
│   │   ├── littlefs_sim_port.c/.h  # 移植层：块设备回调对接模拟基座
│   │   ├── vendor/       #     upstream 源码（lfs.c/lfs_util.*）
│   │   └── test/main_littlefs.c
│   ├── fatfs/            #   开源文件系统（ChaN/FatFs R0.16）
│   │   ├── fatfs_sim_port.c/.h     # 移植层：扇区读改写对接模拟基座
│   │   ├── vendor/       #     upstream 源码（ff.c/ff.h/ffconf.h/...）
│   │   └── test/main_fatfs.c
│   ├── spiffs/           #   开源文件系统（pellepl/SPIFFS）
│   │   ├── spiffs_sim_port.c/.h    # 移植层：HAL 回调对接模拟基座
│   │   ├── spiffs_config.h         # 运行时配置（页/块几何）
│   │   ├── vendor/       #     upstream 源码（spiffs_nucleus/hydrogen/gc/...）
│   │   └── test/main_spiffs.c
│   └── yaffs/            #   开源文件系统（YAFFS2 Direct，GPL v2）
│       ├── yaffs_sim_port.c/.h     # 移植层：NAND chunk+oob 驱动对接模拟基座
│       ├── yaffs_host_types.h      # 宿主类型补丁（loff_t/mode_t 等）
│       ├── vendor/       #     upstream 源码（yaffsfs/guts/tagsmarshall/...）
│       └── test/main_yaffs.c
├── backend/              # 模块三/四后端：仿真服务 + API + 注册表
│   ├── server.py         #   纯标准库 HTTP 服务：列框架 / 编译运行 / 返回结果
│   ├── registry.py       #   框架注册表（框架元数据唯一事实来源）
│   ├── generator.py      #   代码生成引擎（导出库包）
│   └── importer.py       #   导入校验
├── frontend/             # 模块四：前端交互界面
│   ├── index.html        #   选框架 → 运行 → 看结果
│   ├── app.js
│   └── style.css
├── docs/
│   └── prompt_extract_framework.md  # AI 提取提示词：从源项目提取 flash 框架并打包转移
├── scripts/              # 构建/工具脚本
│   ├── run_tests.py/.sh  #   一键编译并运行全部框架测试（--app 跑应用层测试）
│   ├── check_app_build.py#   快速验证全部组件应用层编译
│   └── run_server.sh     #   启动后端服务
├── imports/              # 导入库落地目录（运行时生成，已被 gitignore）
└── README.md
```

> **新增框架** 只需两步：① 源码放入 `frameworks/<name>/`（含 `test/main_*.c`）；
> ② 在 `backend/registry.py` 的 `FRAMEWORKS` 注册表追加一项。前端发现、
> 测试脚本、编译运行都会自动覆盖。需要支持「代码生成导出」时再在
> `backend/generator.py` 的 `RECIPES` 追加配方（当前全部 16 个组件均已接入导出）。

## 快速开始

### 1. 一键运行全部框架测试（推荐）

```bash
./scripts/run_tests.sh                # 运行全部框架测试
./scripts/run_tests.sh kv easyflash   # 只运行指定框架（可多选）
```

输出示例：

```
[OK  ] simulator                运行完成
       === 自检结果: 全部通过 ===
...
================ 测试汇总 ================
[PASS] simulator
[PASS] simulator[NAND]
[PASS] simulator[EEPROM]
[PASS] kv
[PASS] easyflash
[PASS] flashdb
[PASS] baremetal
[PASS] fastflash
[PASS] nvdm
[PASS] fcb
[PASS] nvs
[PASS] zms
[PASS] tym_setting
[PASS] fs
[PASS] littlefs
[PASS] fatfs
[PASS] spiffs
[PASS] yaffs
===========================================
通过 18/18
```

> 测试脚本复用 `backend/registry.py` 注册表，编译/运行参数与后端
> `/api/run` 完全一致（BIN 落盘到各框架 `test/` 目录）。

### 2. 应用层测试（统一任务引擎 + 适配层）

应用层测试框架对不同组件执行**同一任务、同一指标**的横向对比：

```bash
# 批量跑全部支持应用层测试的组件（默认 durability 任务）
python3 scripts/run_tests.py --app

# 只跑指定组件（- 需先进入 venv 或直接 python3 运行）
python3 scripts/run_tests.py --app kv littlefs

# 编译验证全部组件应用层配置
python3 scripts/check_app_build.py
```

命令行直接运行单个组件应用层测试：

```bash
gcc -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=199309L \
    -Iapp -Isimulator -Iframeworks/kv \
    -o /tmp/app_kv simulator/flash_sim.c app/app_register.c app/app_util.c \
    app/app_task.c app/test/main_app.c app/adapter/kv_store_ad.c \
    frameworks/kv/kv_store.c

# APP_COMPONENT 选组件，APP_TASK 选任务（write/read/update/durability/powerloss/mixed）
APP_COMPONENT=kv APP_TASK=durability APP_ITEMS=10 APP_VLEN=32 /tmp/app_kv
```

应用层测试选项（环境变量，均可省略）：

| 变量 | 含义 | 默认 |
|------|------|------|
| `APP_COMPONENT` | 组件 id（kv/easyflash/flashdb/fastflash/nvdm/nvs/zms/fcb/tym_setting/baremetal/fs/littlefs/fatfs/spiffs/yaffs） | 列出已注册组件 |
| `APP_TASK` | write / read / update / durability / powerloss / mixed | durability |
| `APP_ITEMS` | 数据项数量 | 10 |
| `APP_VLEN` | 单条数据长度（字节） | 32 |
| `APP_ROUNDS` | 轮数 | 20 |
| `APP_FREQ` | 每轮修改比例(%) | 50 |
| `APP_CAPACITY` | 组件区容量（0=适配器按介质自决） | 0 |
| `SIM_*` | 模拟基座参数（类型/容量/块/寿命/耗时/坏块） | 见模拟基座 |

输出 `STATS_JSON:{...}` 与 `WEARMAP:...`，含吞吐、写放大、介质阻塞耗时、
磨损分布等应用层独立计算指标。

### 3. 命令行直接验证（手动 gcc）

```bash
# 模拟基座自检
gcc -std=c99 -Wall -Wextra -Isimulator \
    -o /tmp/sim simulator/flash_sim.c simulator/test/main_sim.c && /tmp/sim

# 自研 KV 框架
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/kv \
    -o /tmp/kv simulator/flash_sim.c frameworks/kv/kv_store.c frameworks/kv/test/main_kv.c \
    && /tmp/kv

# EasyFlash（开源 KV 组件）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/easyflash -Iframeworks/easyflash/vendor/inc \
    -o /tmp/ef simulator/flash_sim.c frameworks/easyflash/ef_port.c \
    frameworks/easyflash/vendor/src/ef_env.c frameworks/easyflash/vendor/src/ef_utils.c \
    frameworks/easyflash/vendor/src/easyflash.c frameworks/easyflash/test/main_easyflash.c && /tmp/ef

# FlashDB（开源 KVDB + FAL）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/flashdb -Iframeworks/flashdb/vendor/inc \
    -Iframeworks/flashdb/vendor/fal/inc \
    -o /tmp/fdb simulator/flash_sim.c frameworks/flashdb/fal_flash_sim_port.c \
    frameworks/flashdb/vendor/src/fdb.c frameworks/flashdb/vendor/src/fdb_utils.c \
    frameworks/flashdb/vendor/src/fdb_kvdb.c frameworks/flashdb/vendor/src/fdb_tsdb.c \
    frameworks/flashdb/vendor/src/fdb_file.c frameworks/flashdb/vendor/fal/src/fal.c \
    frameworks/flashdb/vendor/fal/src/fal_flash.c frameworks/flashdb/vendor/fal/src/fal_partition.c \
    frameworks/flashdb/test/main_flashdb.c && /tmp/fdb

# 裸机结构体配置（A/B 双备份 + CRC）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/baremetal \
    -o /tmp/bm simulator/flash_sim.c frameworks/baremetal/bm_config.c \
    frameworks/baremetal/test/main_baremetal.c && /tmp/bm

# fast_flashdb_table（轻量表组件）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/fastflash \
    -Iframeworks/fastflash/vendor/fast_flashdb_table/core \
    -o /tmp/flt simulator/flash_sim.c \
    frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_core.c \
    frameworks/fastflash/vendor/fast_flashdb_table/core/fast_flash_log.c \
    frameworks/fastflash/fastflash_sim_port.c \
    frameworks/fastflash/test/main_fastflash.c && /tmp/flt

# Airoha NVDM（KV/裸机持久化组件）
gcc -std=c99 -Wall -Wextra -DMTK_NVDM_ENABLE -Isimulator -Iframeworks/nvdm \
    -Iframeworks/nvdm/vendor/inc \
    -o /tmp/nvdm simulator/flash_sim.c frameworks/nvdm/nvdm_sim_port.c \
    frameworks/nvdm/vendor/src/nvdm_main.c frameworks/nvdm/vendor/src/nvdm_data.c \
    frameworks/nvdm/vendor/src/nvdm_io.c \
    frameworks/nvdm/test/main_nvdm.c && /tmp/nvdm

# Zephyr FCB（闪存环形缓冲；共享 zephyr_compat 兼容层）
gcc -std=c99 -Wall -Wextra -DCONFIG_FLASH_HAS_EXPLICIT_ERASE \
    -Isimulator -Iframeworks/zephyr_compat -Iframeworks/zephyr_compat/include \
    -Iframeworks/fcb/vendor/include \
    -o /tmp/fcb simulator/flash_sim.c frameworks/zephyr_compat/zephyr_compat.c \
    frameworks/fcb/vendor/fcb.c frameworks/fcb/vendor/fcb_append.c \
    frameworks/fcb/vendor/fcb_elem_info.c frameworks/fcb/vendor/fcb_getnext.c \
    frameworks/fcb/vendor/fcb_rotate.c frameworks/fcb/vendor/fcb_walk.c \
    frameworks/fcb/test/main_fcb.c && /tmp/fcb

# Zephyr NVS（KV/裸机持久化）
gcc -std=c99 -Wall -Wextra -DCONFIG_FLASH_HAS_EXPLICIT_ERASE \
    -Isimulator -Iframeworks/zephyr_compat -Iframeworks/zephyr_compat/include \
    -Iframeworks/nvs/vendor/include \
    -o /tmp/nvs simulator/flash_sim.c frameworks/zephyr_compat/zephyr_compat.c \
    frameworks/nvs/vendor/nvs.c frameworks/nvs/test/main_nvs.c && /tmp/nvs

# Zephyr ZMS（KV/固定槽位存储）
gcc -std=c99 -Wall -Wextra -DCONFIG_FLASH_HAS_EXPLICIT_ERASE \
    -Isimulator -Iframeworks/zephyr_compat -Iframeworks/zephyr_compat/include \
    -Iframeworks/zms/vendor/include \
    -o /tmp/zms simulator/flash_sim.c frameworks/zephyr_compat/zephyr_compat.c \
    frameworks/zms/vendor/zms.c frameworks/zms/test/main_zms.c && /tmp/zms

# TYM Setting（ID静态表/RAM镜像，去耦裁剪）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/tym_setting \
    -Iframeworks/tym_setting/vendor/inc -Iframeworks/tym_setting/config \
    -Iframeworks/tym_setting/compat \
    -o /tmp/tym simulator/flash_sim.c frameworks/tym_setting/tym_setting_sim_port.c \
    frameworks/tym_setting/vendor/src/app_setting_idle_activity.c \
    frameworks/tym_setting/vendor/src/StorageDrv.c \
    frameworks/tym_setting/test/main_tym_setting.c && /tmp/tym

# 自研文件系统框架
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/fs \
    -o /tmp/fs simulator/flash_sim.c frameworks/fs/fs_store.c \
    frameworks/fs/test/main_fs.c && /tmp/fs

# LittleFS（开源文件系统）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/littlefs -Iframeworks/littlefs/vendor \
    -o /tmp/lfs simulator/flash_sim.c frameworks/littlefs/littlefs_sim_port.c \
    frameworks/littlefs/vendor/lfs.c frameworks/littlefs/vendor/lfs_util.c \
    frameworks/littlefs/test/main_littlefs.c && /tmp/lfs

# FatFs（开源文件系统）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/fatfs -Iframeworks/fatfs/vendor \
    -o /tmp/ff simulator/flash_sim.c frameworks/fatfs/fatfs_sim_port.c \
    frameworks/fatfs/vendor/ff.c frameworks/fatfs/vendor/ffsystem.c \
    frameworks/fatfs/vendor/ffunicode.c frameworks/fatfs/test/main_fatfs.c && /tmp/ff

# SPIFFS（开源文件系统）
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/spiffs -Iframeworks/spiffs/vendor \
    -o /tmp/spiffs simulator/flash_sim.c frameworks/spiffs/spiffs_sim_port.c \
    frameworks/spiffs/vendor/spiffs_nucleus.c frameworks/spiffs/vendor/spiffs_hydrogen.c \
    frameworks/spiffs/vendor/spiffs_gc.c frameworks/spiffs/vendor/spiffs_check.c \
    frameworks/spiffs/vendor/spiffs_cache.c \
    frameworks/spiffs/test/main_spiffs.c && /tmp/spiffs

# YAFFS（开源文件系统，NAND chunk+oob 布局）
gcc -std=c99 -DCONFIG_YAFFS_DIRECT -DCONFIG_YAFFS_DEFINES_TYPES \
    -DCONFIG_YAFFS_PROVIDE_DEFS -DCONFIG_YAFFSFS_PROVIDE_VALUES \
    -include frameworks/yaffs/yaffs_host_types.h \
    -Isimulator -Iframeworks/yaffs -Iframeworks/yaffs/vendor \
    -o /tmp/yaffs simulator/flash_sim.c frameworks/yaffs/yaffs_sim_port.c \
    frameworks/yaffs/vendor/yaffsfs.c frameworks/yaffs/vendor/yaffs_guts.c \
    frameworks/yaffs/vendor/yaffs_allocator.c frameworks/yaffs/vendor/yaffs_tagscompat.c \
    frameworks/yaffs/vendor/yaffs_tagsmarshall.c frameworks/yaffs/vendor/yaffs_nand.c \
    frameworks/yaffs/vendor/yaffs_checkptrw.c frameworks/yaffs/vendor/yaffs_packedtags1.c \
    frameworks/yaffs/vendor/yaffs_packedtags2.c frameworks/yaffs/vendor/yaffs_bitmap.c \
    frameworks/yaffs/vendor/yaffs_verify.c frameworks/yaffs/vendor/yaffs_nameval.c \
    frameworks/yaffs/vendor/yaffs_attribs.c frameworks/yaffs/vendor/yaffs_yaffs1.c \
    frameworks/yaffs/vendor/yaffs_yaffs2.c frameworks/yaffs/vendor/yaffs_ecc.c \
    frameworks/yaffs/vendor/yaffs_hweight.c frameworks/yaffs/vendor/yaffs_summary.c \
    frameworks/yaffs/vendor/yaffs_endian.c frameworks/yaffs/vendor/yaffs_error.c \
    frameworks/yaffs/test/main_yaffs.c && /tmp/yaffs
```

### 4. 前端（模拟运行 + 应用层测试 + 代码生成 + 导入闭环）

```bash
./scripts/run_server.sh --port 8000   # 等价于 python3 backend/server.py --port 8000
# 浏览器打开 http://localhost:8000
```

页面流程：
0. **左侧框架分类展示**：框架按「驱动层 · 模拟基座 / 组件层 · 裸机简单管理 /
   组件层 · KV 管理 / 组件层 · 文件系统」分组列出。
1. **选择框架并运行测试**：选内置/已导入框架 → 展开「模拟基座配置」与「测试配置」表单
   （可配置介质类型/容量/擦除块大小/最小写入单位/标称寿命/读写擦耗时/坏块数量与比率、
   KV 条目数/长度/修改轮数/修改频率等）→ 「运行测试」→ 逐行结果 + 性能统计卡片 +
   整片**磨损分布柱状图**（颜色越红越接近寿命上限）。
2. **代码生成（导出库文件）**：选框架、填参数 → 「生成并下载」→ 下载 zip 包。
   包内按 `core/vendor/port/config/include/demo` 分层组织，附 `README.md` 总览、
   `PORTING.md` 完整移植文档（API/HAL 契约/配置/编译/限制/验证）、
   `HAL_CONTRACT.md` 统一适配接口契约、`AI_PORTING_PROMPT.md`（投喂给目标
   工程 AI 的移植提示词）、自检入口 `demo/test_main.c` 与 `demo/BUILD.md`
   （可一键在 PC 冒烟）。全部 16 个组件均可导出：kv / simulator / easyflash /
   flashdb / baremetal / fs / littlefs / fatfs / spiffs / yaffs / fcb / nvs /
   zms / tym_setting / fastflash / nvdm。
3. **应用层测试（跨组件对比）**：在「应用层测试」标签页选择组件与任务
   （写入/读取/更新/耐久/掉电安全/混合），可调数据项数/长度/轮数等，也可一键
   「批量跑全部组件」——统一任务引擎通过适配层调用各组件，实时输出性能统计
   （吞吐、写放大、介质阻塞、磨损分布）做横向对比。
4. **导入库文件（闭环验证）**：上传刚下载的 zip → 后端校验是否符合模拟基座接口要求
   （`manifest.requires=="flash_sim"` 且源码 `#include "flash_sim.h"`）、编译运行自带自检 →
   通过后注册为可用框架，出现在①中可直接「运行测试」。

**模拟基座可配置指标**：类型(NOR/NAND/EEPROM)、总容量、擦除块大小、最小写入单位、
标称擦写寿命、读/写/擦耗时(us)、固定坏块数量、运行时坏块比率。运行时统一统计读/写/擦次数、
累计字节、耗时(us)、最大/平均擦写次数、坏块数，并暴露每块磨损数组供绘图。

**KV 功能压测模式**（`KV_FUNC=1`）：可配置条目数量、每条 value 长度、修改轮数、每轮修改比例；
运行后统计总操作数、数据丢失数、阻塞耗时(读+写+擦耗时之和，模拟 RTOS 下被阻塞时长)，
并输出整片磨损分布。

## 嵌入式文件系统支持矩阵

常见嵌入式文件系统在本平台的适配情况：

| 文件系统 | 说明 | 平台状态 |
|----------|------|----------|
| **LittleFS** | ARM 官方，掉电安全 + 磨损均衡，面向 MCU/NOR | ✅ 已移植（v2.x） |
| **SPIFFS** | SPI NOR 小文件系统，日志结构 + GC | ✅ 已移植 |
| **FatFs / FAT** | 通用 FAT12/16/32（可开 exFAT），兼容性好 | ✅ 已移植（R0.16） |
| **YAFFS / YAFFS2** | 专为 NAND 设计，损耗均衡 + 掉电保护 | ✅ 已移植（Direct） |
| **JFFS2** | NOR/NAND 日志型，Linux MTD 内核原生 | ❌ 依赖 Linux MTD 子系统，不适合独立移植 |
| **UBIFS** | NAND 日志型，依赖 UBI/MTD 内核 | ❌ 同上，无法脱离内核运行 |
| **SquashFS** | 只读压缩文件系统，Linux 内核组件 | ❌ 内核模块，非嵌入式可独立移植组件 |
| **ext2/3/4** | Linux 桌面/嵌入式常用，依赖 VFS | ❌ 依赖 Linux VFS 层，无法独立移植 |
| **initramfs** | Linux 启动用的临时内存 FS | ❌ 非独立文件系统框架 |

> 可独立移植的嵌入式文件系统（LittleFS/SPIFFS/FatFs/YAFFS）均已接入本平台，
> 可通过前端一键运行测试、导出库包；依赖 Linux 内核（MTD/UBI/VFS）的系统
> 不在本仿真平台范围内。

## 架构链路

### 运行链路

```
前端(浏览器) → 后端 API(Python)
                ├─ 代码生成引擎(generator.py) → 导出 zip 库包
                ├─ 导入校验(importer.py)     → 解压/校验/编译运行/注册
                ├─ 应用层测试(/api/app/run)   → 编译运行 app/main_app.c（统一任务引擎）
                └─ 仿真服务                    → 编译运行 C 测试程序 → 模拟基座 → BIN 物理介质
```

### 存储软件分层（三层架构）

```
┌──────────────────────────────────────────────────────────────┐
│ 应用层测试框架 (app/)                                         │
│  统一任务引擎(任务+选项) → 独立性能计算(吞吐/写放大/磨损)      │
│        │ 通过适配层调用组件，不感知组件内部实现                │
├──────────────────────────────────────────────────────────────┤
│ 组件层 (frameworks/)                                          │
│  裸机简单框架(baremetal)  KV 管理(kv/easyflash/flashdb/       │
│  fastflash/nvdm/nvs/zms/fcb/tym_setting) 文件系统(fs/littlefs/│
│  fatfs/spiffs/yaffs)                                          │
│  每个组件 = vendor 源码(零修改) + sim_port 移植层              │
│        │ 仅调用 flash_sim 统一接口                            │
├──────────────────────────────────────────────────────────────┤
│ 驱动层 (simulator/flash_sim)                                  │
│  NOR/NAND/EEPROM 物理仿真：块擦除/位翻转/寿命/坏块/耗时        │
│  独立性能检测与参数配置                                        │
└──────────────────────────────────────────────────────────────┘
```

后端模块职责：

- `registry.py`：框架注册表（元数据：源码组成/编译参数/测试项/schema/
  分类/app 适配器映射），被 `server.py`、测试脚本共享，新增框架只改此处。
- `server.py`：HTTP 服务 + 编译运行 + SSE 流式推送 + 应用层测试接口
  `/api/app/run(/stream)`（纯标准库）。
- `generator.py`：导出库包生成。
- `importer.py`：导入 zip 校验与注册。

所有生成的 C 库均可脱离本平台，直接移植到真实 MCU Flash 驱动（仅需按
`include/flash_sim.h` 契约将 `flash_sim_*` 替换为真实驱动实现，见包内
`PORTING.md` 与 `HAL_CONTRACT.md`；也可将包目录连同 `AI_PORTING_PROMPT.md`
交给目标工程里的 AI 完成移植适配）。

## 从其他项目提取 Flash 框架（AI 提示词）

需要把其他项目中的 Flash 存储组件（KV / 文件系统 / 裸机配置）导入本平台
对比测试时，分两步完成：

1. **源项目侧（提取打包）**：把 `docs/prompt_extract_framework.md` 交给
   源项目里的 AI。该 AI 看不到本平台代码，只需定位并提取框架本体
   （vendor 零修改），整理成自包含目录（含 `PORTING.md` 移植文档、
   `MANIFEST.md` 提取清单、`bsp_reference/` 平台适配参考），打包成
   压缩包交付。
2. **本平台侧（移植适配）**：拿到压缩包后放入 `flash_use`，由本平台 AI
   依据包内 `PORTING.md` 的 HAL 接口规格与 `bsp_reference/` 参考样例，
   编写移植层（sim_port）与适配器（app/adapter），接入应用层测试框架
   做横向性能对比。

## 包格式（导入契约）

导出 zip 采用分层结构（导入时自动剥离顶层 `<lib>_library/` 目录）：

```
<lib>_library/
├── README.md           总览：快速上手 + 目录结构 + 文档索引
├── PORTING.md          完整移植文档（API / HAL 契约 / 配置 / 编译 / 集成 / 限制 / 验证）
├── HAL_CONTRACT.md     统一适配接口契约（所有框架一致的 Flash 操作抽象）
├── AI_PORTING_PROMPT.md 给目标工程 AI 的移植提示词（可直接投喂）
├── manifest.json       { id, name, requires:"flash_sim", entry, lib,
                           lib_sources:[...], cflags, includes, params }
├── core/               框架核心（平台无关）或 HAL 参考实现 core/flash_sim.c
├── vendor/             开源/厂商源码（零修改，只读）
├── port/               平台移植层（目标平台替换/重写点）
├── config/             配置文件模板（分区/几何参数）
├── include/            对外公共头 + HAL 契约头 include/flash_sim.h
└── demo/               自检入口 test_main.c + 构建说明 BUILD.md（可 PC 一键冒烟）
```

导入校验规则：缺 manifest、requires≠flash_sim、源码未依赖 flash_sim.h、
或编译/运行自带自检失败，均会被拒绝并给出原因。开源组件（easyflash/flashdb）
导出包内含上游多源文件，运行时会一并编译 `lib_sources` 列出的库源；zephyr 系
组件（fcb/nvs/zms）保留 `<zephyr/...>` 头路径结构，由 `manifest.includes`
声明 include 目录。

## 完成状态

- [x] 模块一 模拟基座（NOR/NAND/EEPROM + BIN 落盘自检 + **可配置类型/容量/块大小/寿命/速度/坏块** + 性能与磨损统计 + 磨损分布导出）
- [x] 模块二 自研 KV/NVS 框架（掉电安全 + CRC + 压实 GC + **功能压测模式：条目数/长度/修改频率/数据丢失/阻塞耗时**）+ 运行验证
- [x] 模块二 开源 KV 组件 **EasyFlash**（EF NG 模式 ENV/KV：磨损均衡 + 掉电保护 + GC），可切换、可独立导出，模拟基座验证 0 擦写/GC 错误
- [x] 模块二 开源 KV 组件 **FlashDB**（KVDB + FAL：磨损均衡 + 掉电保护 + GC + blob/遍历），模拟基座验证 0 擦写/GC 错误
- [x] 模块二 裸机结构体配置框架（A/B 双备份 + CRC32 + 单调序号掉电恢复 + 磨损分摊），模拟基座验证 0 数据丢失
- [x] 模块二 开源组件 **fast_flashdb_table**（轻量表存储：建表/按索引读写/追加/删除/GC/掉电重放），经移植层对接模拟基座运行验证通过
- [x] 模块二 厂商组件 **Airoha NVDM**（KV/裸机持久化：PEB 磨损均衡 + 掉电保护状态机 + 数据项校验和 + GC），经 nvdm_port_* 契约移植层对接模拟基座运行验证通过
- [x] 模块二 开源组件 **Zephyr FCB**（闪存环形缓冲：append-only 日志 + 回卷覆盖 + CRC + 掉电恢复），经 Zephyr 兼容层桥接模拟基座运行验证通过
- [x] 模块二 开源组件 **Zephyr NVS**（KV/裸机持久化：扇区式 ATE 日志 + 掉电安全 + GC），经 Zephyr 兼容层桥接模拟基座运行验证通过
- [x] 模块二 开源组件 **Zephyr ZMS**（KV/固定槽位存储：磨损均衡 + 掉电安全，定位替代 NVS），经 Zephyr 兼容层桥接模拟基座运行验证通过
- [x] 模块二 厂商组件 **TYM Setting**（ID 静态表 + RAM 镜像 + 延时整页回写；去 FreeRTOS/ui_shell/hal_log 耦合后经移植层对接模拟基座，写读改/延时回写/重启持久化验证通过）
- [x] 模块二 自研文件系统框架 **fs_store**（块分配表 + 数据块：多文件创建/读写、单文件频繁修改、追加、删除、查询、掉电重放），模拟基座验证通过
- [x] 模块二 开源文件系统 **LittleFS**（littlefs-project v2.x：掉电安全 + 磨损均衡），经移植层对接模拟基座运行验证通过
- [x] 模块二 开源文件系统 **FatFs**（ChaN R0.16：FAT12/16 格式化 + 多文件操作），经扇区读改写移植层对接模拟基座运行验证通过
- [x] 模块二 开源文件系统 **SPIFFS**（pellepl：SPI NOR 掉电安全 + 垃圾回收），经 HAL 回调移植层对接模拟基座运行验证通过
- [x] 模块二 开源文件系统 **YAFFS**（YAFFS2 Direct，GPL v2：NAND 日志型 + 检查点 + 磨损均衡），经 chunk+oob 驱动移植层对接模拟基座运行验证通过
- [x] 模块三 代码生成引擎（导出库包：core/vendor/port/config/include/demo 分层 + 统一 HAL 契约 + 完整 PORTING.md + **AI 移植提示词**，全部 16 组件可导出）+ 导入闭环校验（保留子目录结构，支持 manifest.includes/相对 cflags）
- [x] 模块四 前端模拟运行界面（选框架 / **配置化表单（基座+测试）** / 性能统计卡片 / **磨损柱状图** / 生成下载 / 导入验证）
- [x] 工程化 统一测试脚本（`scripts/run_tests.sh` 一键跑全部 18 项测试）、框架注册表独立（`backend/registry.py`）、后端/导入支持框架级 `cflags`
- [x] **应用层测试框架（三层架构）**：驱动层(模拟基座) → 组件层(裸机/KV/文件系统) → 应用层测试框架
- [x] **应用层统一任务引擎**（`app/app_task.c`）：write/read/update/durability/powerloss/mixed 六类任务
- [x] **适配层**（`app/adapter/*`）：15 个组件统一适配为 `app_component_t`，组件源码零修改，支持掉电恢复（APP_REINIT）
- [x] **应用层独立性能计算**：吞吐(ops/s, KB/s)、写放大、介质阻塞耗时、磨损分布、数据丢失校验
- [x] **前端分类展示**：框架按 驱动层/裸机/KV/文件系统 分组；新增「应用层测试」标签页（单组件/批量对比 + 实时性能卡片）
- [x] **后端应用层接口**：`/api/app/run` 与 `/api/app/run/stream`（SSE 流式），编译配置由 registry `app_layer_for` 推导
- [x] **框架提取提示词**：`docs/prompt_extract_framework.md` 指导源项目 AI 提取 flash 框架并打包（vendor 零修改 + PORTING.md 移植文档），由本平台 AI 依据包内文档完成移植适配
- [ ] 模块三 AI 接口（规划中，本次未实现）
- [ ] 模块二 OTA 差分框架（规划中）
