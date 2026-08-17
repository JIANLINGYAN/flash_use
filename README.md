# Flash 存储仿真平台

嵌入式 Flash 存储管理设计与仿真平台。当前已完成 **模块一（模拟基座）** 与
**模块二（KV/NVS 存储框架）**，并提供 **前端模拟运行界面** 进行可视化验证。

## 项目结构

```
flash_use/
├── simulator/            # 模块一：模拟基座（C 库）
│   ├── flash_sim.h/.c    #   统一 Flash 接口 read/write/erase，BIN 落盘
│   └── test/main_sim.c   #   自检：NOR/NAND/EEPROM 物理特性
├── frameworks/           # 模块二：存储框架库（对接 simulator，可移植 C）
│   └── kv/               #   KV/NVS 框架
│       ├── kv_store.h/.c #     两步提交掉电安全 + CRC + 压实 GC
│       └── test/main_kv.c#     运行验证
├── backend/              # 模块三/四后端：仿真服务 + API
│   └── server.py         #   纯标准库 HTTP 服务：列框架 / 编译运行 / 返回结果
├── frontend/             # 模块四：前端交互界面
│   ├── index.html        #   选框架 → 运行 → 看结果
│   ├── app.js
│   └── style.css
└── scripts/              # 构建/工具脚本（预留）
```

> 后续框架（文件系统 fs / 裸机双备份 baremetal / OTA 差分 ota）按
> `frameworks/<name>/` 组织，并在 `backend/server.py` 的 FRAMEWORKS 注册表
> 追加一项即可被前端自动发现。

## 快速开始

### 1. 命令行直接验证（无需前端）
```bash
# 模拟基座自检
gcc -std=c99 -Wall -Wextra -Isimulator \
    -o /tmp/sim simulator/flash_sim.c simulator/test/main_sim.c && /tmp/sim

# KV 框架验证
gcc -std=c99 -Wall -Wextra -Isimulator -Iframeworks/kv \
    -o /tmp/kv simulator/flash_sim.c frameworks/kv/kv_store.c frameworks/kv/test/main_kv.c \
    && /tmp/kv
```

### 2. 前端模拟运行界面
```bash
python3 backend/server.py --port 8000
# 浏览器打开 http://localhost:8000
```
- 选择框架（模拟基座 / KV/NVS）
- 点击「运行测试」→ 后端编译并运行对应 C 测试程序
- 前端实时展示逐行结果（OK 绿 / FAIL 红）与汇总（通过/失败/耗时/结论）

## 架构链路
```
前端(浏览器) → 后端 API(Python) → 编译运行 C 测试程序 → 模拟基座 → BIN 物理介质
```
所有生成的 C 库均可脱离本平台，直接移植到真实 MCU Flash 驱动（仅需将
`flash_sim_*` 替换为真实驱动实现）。

## 完成状态
- [x] 模块一 模拟基座（NOR/NAND/EEPROM + BIN 落盘自检）
- [x] 模块二 KV/NVS 框架（掉电安全 + CRC + 压实 GC）+ 运行验证
- [x] 模块四 前端模拟运行界面（选框架 / 运行 / 看结果）
- [ ] 模块三 AI 接口与代码生成引擎（规划中）
- [ ] 模块二 文件系统 / 裸机双备份 / OTA 差分框架（规划中）
