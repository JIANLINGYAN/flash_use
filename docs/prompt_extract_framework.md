# 提示词：从其他项目提取 Flash 存储框架（对比测试）

> 本文件是给 AI 使用的**框架提取提示词**。当你拿到一个陌生的、含 Flash
> 存储组件（KV / 文件系统 / 裸机结构体配置）的开源项目时，把下面的提示词
> 发给 AI，让它按统一规范把该框架**提取并移植**到本平台
> （`flash_use`），从而与平台内已有组件在**同一模拟基座**上做横向对比测试。
>
> 目标：**组件层源码零修改**，只新增"移植层"与"适配器"，由应用层统一
> 任务引擎评估吞吐 / 写放大 / 磨损 / 掉电安全。

---

## 你要做的事

用户会给你：一个开源项目的**仓库地址或源码目录**，以及它使用的**存储组件
名称**（例如 littlefs、FatFs、SPIFFS、EasyFlash、FlashDB、NVS 等）。

你的任务是：

1. 分析该项目中的存储框架源码，确定其**存储语义类别**：
   - `kv`：键值存储（set / get / delete）
   - `fs`：文件系统（create / write / read / append / delete）
   - `baremetal`：裸机结构体配置（整块结构体 save / load）
2. 只提取**框架本体源码**（vendor 部分），不提取业务代码。
3. 在本平台新增一个框架目录 `frameworks/<name>/`，包含：

```
frameworks/<name>/
├── vendor/                 # 开源框架原始源码（零修改）
│   └── ...                 # 框架 .c/.h（保留作者原样，不改动）
├── <name>_sim_port.c/.h    # 移植层：把框架的 Flash 读写擦接口
│                           # 桥接到 simulator/flash_sim.h（模拟基座）
├── test/main_<name>.c      # 组件自带自检入口（可选）
└── PORTING.md              # 移植说明（如何对接、参数含义）
```

---

## 移植层（sim_port）编写规范

移植层是"框架 <-> 模拟基座"的唯一桥接，**只做参数与句柄的桥接，
不含业务逻辑**。必须：

- `#include "flash_sim.h"`，使用 `flash_sim_init / read / write / erase`。
- 通过环境变量读配置（与本平台一致）：
  `SIM_TYPE`（0=NOR 1=NAND 2=EEPROM）、`SIM_TOTAL`、`SIM_ERASE`、
  `SIM_WRITE`、`SIM_CYCLES`、`SIM_RD_US`、`SIM_WR_US`、`SIM_ERASE_US`、
  `SIM_BAD_N`、`SIM_BAD_R`。参考：`frameworks/easyflash/ef_port.c`、
  `frameworks/littlefs/littlefs_sim_port.c`。
- 若框架要求"按块擦除 / 页写"语义（NOR/NAND），而介质类型为 EEPROM
  （无块擦除），需在移植层显式处理或拒绝（返回错误并说明原因）。
- 提供 `xxx_sim_init_device(bin_path)` 与 `xxx_sim_deinit_device()`，
  供测试程序/适配器调用；对外暴露设备句柄读取函数
  `flash_dev_t *xxx_sim_device(void)`（供应用层统计磨损）。

## 适配器（app/adapter）编写规范

应用层测试框架通过**适配器**把组件包装成统一接口 `app_component_t`：

- 文件放在 `app/adapter/<name>_ad.c`。
- 实现 `app_component_t`：
  - `id`：唯一 id（建议小写英文）；`category`：`kv` / `fs` / `baremetal`；
  - `init(opt)`：构造模拟基座配置 + 初始化移植层 + 初始化框架；
  - `kv_set/kv_get/kv_del`（kv 类）或 `fs_write/fs_read/fs_append/
    fs_delete/fs_get_size`（fs 类）或 `bm_save/bm_load`（baremetal 类）；
  - `device()`：返回底层模拟基座句柄。
- 文件末尾用 `__attribute__((constructor))` 调用 `app_register(&comp)`
  自注册。
- **掉电安全任务要求**：适配器 `init()` 需支持 `APP_REINIT=1` 时
  "跳过擦除/格式化/删 bin 文件、直接重新挂载"（模拟掉电重启后数据
  仍在），否则 powerloss 任务无法通过。
- 参考：`app/adapter/easyflash_ad.c`、`app/adapter/littlefs_ad.c`。

## 验证与注册

1. 编译验证：`python3 scripts/check_app_build.py <name>`
   （如新增组件需要，请把编译依赖加入 `backend/registry.py` 的
   `FRAMEWORKS`，并把适配器路径加入 `APP_ADAPTER_MAP`）。
2. 功能验证：`python3 scripts/run_tests.py --app <name>`
   （应用层统一任务引擎默认跑 durability）。
3. 在 `backend/registry.py` 注册框架条目：
   - `id / name / category / desc / sources / includes / workdir`；
   - `test_schema`（组件自带自检参数）；`config_schema`（模拟基座配置）。
4. 补一份 `frameworks/<name>/PORTING.md`，记录：框架版本、移植改动点、
   对接参数、已知限制（如表数量上限、写放大特性）。

## 输出格式（请严格遵守）

请以如下结构汇报你的提取结果：

```
## 提取结果：<组件名>
- 类别：kv / fs / baremetal
- 源码来源：<仓库/目录>（版本/commit 可选）
- 是否修改 vendor 源码：否（零修改原则）
- 新增文件：
  - frameworks/<name>/vendor/...（<文件清单>）
  - frameworks/<name>/<name>_sim_port.c/.h
  - app/adapter/<name>_ad.c
- 移植要点：
  - Flash 接口如何对接模拟基座（读写擦）
  - 关键宏/配置
  - 已知限制
- 验证：
  - `python3 scripts/check_app_build.py <name>` 结果
  - `python3 scripts/run_tests.py --app <name>` 结果
```

---

## 常见问题

- **框架要求 FLASH_SECTOR_SIZE 等编译期常量**：保留原值，若与模拟基座
  的 `SIM_ERASE` 不一致，在移植层读取环境变量后传入 `flash_sim_erase`
  的 size 需与框架一致；如无法运行时配置，需在 PORTING.md 说明。
- **框架无掉电恢复 API**：适配器在 `APP_REINIT=1` 时跳过格式化并重新
  挂载，由模拟基座保证介质内容保留（bin 文件持久化）。
- **写放大极大的框架**（如每次覆盖写复制整表）：不要修改 vendor，在
  适配层选择更优的语义映射（如单表多索引），并在 PORTING.md 记录其
  固有写放大特性，应用层测试会如实统计。
- **不要在 vendor 里加注释或改格式**：保持开源代码原样，便于版本升级
  与 diff 对比。
