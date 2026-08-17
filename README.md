# Flash 存储仿真平台

嵌入式 Flash 存储管理设计与仿真平台。已完成 **模块一（模拟基座）**、
**模块二（KV/NVS 框架 + 常见开源 KV 组件 + 裸机双备份框架）**，并提供
**前端模拟运行界面** 进行可视化验证。

## 项目结构

```
flash_use/
├── simulator/            # 模块一：模拟基座（C 库）
│   ├── flash_sim.h/.c    #   统一 Flash 接口 read/write/erase，BIN 落盘
│   └── test/main_sim.c   #   自检：NOR/NAND/EEPROM 物理特性
├── frameworks/           # 模块二：存储框架库（对接 simulator，可移植 C）
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
│   └── baremetal/        #   裸机结构体配置框架（A/B 双备份 + CRC32 + 单调序号）
│       ├── bm_config.h/.c #     结构体整块映射、掉电恢复
│       └── test/main_baremetal.c
├── backend/              # 模块三/四后端：仿真服务 + API
│   ├── server.py         #   纯标准库 HTTP 服务：列框架 / 编译运行 / 返回结果
│   ├── generator.py      #   代码生成引擎（导出库包）
│   └── importer.py       #   导入校验
├── frontend/             # 模块四：前端交互界面
│   ├── index.html        #   选框架 → 运行 → 看结果
│   ├── app.js
│   └── style.css
└── scripts/              # 构建/工具脚本（预留）
```

> 新增框架按 `frameworks/<name>/` 组织，并在 `backend/server.py` 的
> FRAMEWORKS 注册表追加一项即可被前端自动发现；`backend/generator.py`
> 的 RECIPES 追加一项即可支持导出。

## 快速开始

### 1. 命令行直接验证（无需前端）
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
```

### 2. 前端（模拟运行 + 代码生成 + 导入闭环）
```bash
python3 backend/server.py --port 8000
# 浏览器打开 http://localhost:8000
```
页面三段式流程：
1. **选择框架并运行测试**：选内置/已导入框架 → 展开「模拟基座配置」与「测试配置」表单
   （可配置介质类型/容量/擦除块大小/最小写入单位/标称寿命/读写擦耗时/坏块数量与比率、
   KV 条目数/长度/修改轮数/修改频率等）→ 「运行测试」→ 逐行结果 + 性能统计卡片 +
   整片**磨损分布柱状图**（颜色越红越接近寿命上限）。
2. **代码生成（导出库文件）**：选框架、填参数 → 「生成并下载」→ 下载 zip 包
   （含 `.c/.h` + `test_main.c` 自检入口 + `PORTING.md` 移植说明 + `manifest.json`）。
3. **导入库文件（闭环验证）**：上传刚下载的 zip → 后端校验是否符合模拟基座接口要求
   （`manifest.requires=="flash_sim"` 且源码 `#include "flash_sim.h"`）、编译运行自带自检 →
   通过后注册为可用框架，出现在①中可直接「运行测试」。

**模拟基座可配置指标**：类型(NOR/NAND/EEPROM)、总容量、擦除块大小、最小写入单位、
标称擦写寿命、读/写/擦耗时(us)、固定坏块数量、运行时坏块比率。运行时统一统计读/写/擦次数、
累计字节、耗时(us)、最大/平均擦写次数、坏块数，并暴露每块磨损数组供绘图。

**KV 功能压测模式**（`KV_FUNC=1`）：可配置条目数量、每条 value 长度、修改轮数、每轮修改比例；
运行后统计总操作数、数据丢失数、阻塞耗时(读+写+擦耗时之和，模拟 RTOS 下被阻塞时长)，
并输出整片磨损分布。

## 架构链路
```
前端(浏览器) → 后端 API(Python)
                ├─ 代码生成引擎(generator.py) → 导出 zip 库包
                ├─ 导入校验(importer.py)     → 解压/校验/编译运行/注册
                └─ 仿真服务                    → 编译运行 C 测试程序 → 模拟基座 → BIN 物理介质
```
所有生成的 C 库均可脱离本平台，直接移植到真实 MCU Flash 驱动（仅需将
`flash_sim_*` 替换为真实驱动实现，见包内 `PORTING.md`）。

## 包格式（导入契约）
```
xxx_library.zip
├── <lib>.c / <lib>.h   核心库（平台无关，可能含多个 .c）
├── test_main.c         标自检入口（对接 flash_sim.h）
├── PORTING.md          移植说明
└── manifest.json       { id, name, requires:"flash_sim", entry, lib,
                          lib_sources:[...], params }
```
导入校验规则：缺 manifest、requires≠flash_sim、源码未依赖 flash_sim.h、
或编译/运行自带自检失败，均会被拒绝并给出原因。开源组件（easyflash/flashdb）
导出包内含上游多源文件，运行时会一并编译 `lib_sources` 列出的库源。

## 完成状态
- [x] 模块一 模拟基座（NOR/NAND/EEPROM + BIN 落盘自检 + **可配置类型/容量/块大小/寿命/速度/坏块** + 性能与磨损统计 + 磨损分布导出）
- [x] 模块二 自研 KV/NVS 框架（掉电安全 + CRC + 压实 GC + **功能压测模式：条目数/长度/修改频率/数据丢失/阻塞耗时**）+ 运行验证
- [x] 模块二 开源 KV 组件 **EasyFlash**（EF NG 模式 ENV/KV：磨损均衡 + 掉电保护 + GC），可切换、可独立导出，模拟基座验证 0 擦写/GC 错误
- [x] 模块二 开源 KV 组件 **FlashDB**（KVDB + FAL：磨损均衡 + 掉电保护 + GC + blob/遍历），模拟基座验证 0 擦写/GC 错误
- [x] 模块二 裸机结构体配置框架（A/B 双备份 + CRC32 + 单调序号掉电恢复 + 磨损分摊），模拟基座验证 0 数据丢失
- [x] 模块三 代码生成引擎（导出库包：.c/.h + 移植说明，支持 kv/simulator/easyflash/flashdb/baremetal）+ 导入闭环校验
- [x] 模块四 前端模拟运行界面（选框架 / **配置化表单（基座+测试）** / 性能统计卡片 / **磨损柱状图** / 生成下载 / 导入验证）
- [ ] 模块三 AI 接口（规划中，本次未实现）
- [ ] 模块二 文件系统 / OTA 差分框架（规划中）
