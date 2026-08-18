# Airoha NVDM 组件库移植说明（PORTING）

## 1. 组件概述

- 名称：Airoha NVDM（Non-Volatile Data Management）
- 来源：Airoha BT audio SDK（`mcu/middleware/airoha/nvdm` + `nvdm_core`），厂商专有许可
- 存储语义类别：**KV（键值）+ 裸机/RTOS 持久化 + 分区(PEB)磨损均衡**
- 功能：数据项按"组+项"两级命名存储；PEB 块磨损均衡、掉电保护状态机、
  数据项校验和、垃圾回收（GC）、空间/条目查询
- 适用介质：NOR Flash（按块擦除、字节写，写入仅允许 1->0）

## 2. 目录结构

```
frameworks/nvdm/
├── vendor/                    # 框架本体（零修改）
│   ├── inc/                   # nvdm.h / nvdm_port.h / nvdm_internal.h / nvdm_msgid_log.h
│   └── src/                   # nvdm_main.c / nvdm_data.c / nvdm_io.c
├── nvdm_sim_port.c            # 移植层：实现 nvdm_port_* 契约，桥接 flash_sim
├── nvdm_sim_port.h            # 移植层头（分区配置注入接口）
└── test/main_nvdm.c           # 组件自检
```

> 原始提取包中的 `bsp_reference/`（Airoha 平台移植参考）、`config/`（Airoha
> 板级配置）、`nvdm_cli.c`（依赖 minicli）、`nvkey.c`（依赖业务 ID 表）属于
> 源项目耦合内容，未纳入本组件库。如需参考，见提交记录中的原始提取包。

## 3. 对外 API（vendor/inc/nvdm.h）

初始化：`nvdm_init()`（仅一次，调用前须 `nvdm_sim_setup` 注入分区配置）。

核心操作（group+item 两级命名，`group` 建议固定 "app"）：

| 函数 | 说明 |
|------|------|
| `nvdm_write_data_item(g,i,type,buf,size)` | 写/更新，type 支持 RAW/STRING |
| `nvdm_read_data_item(g,i,buf,&size)` | 读；size 入参为缓冲区大小，出参为实际大小 |
| `nvdm_delete_data_item(g,i)` | 删除单项 |
| `nvdm_delete_group(g)` / `nvdm_delete_all()` | 删组 / 全删 |
| `nvdm_query_begin/next_group_name/next_data_item_name/end` | 遍历 |
| `nvdm_trigger_garbage_collection(part, NVDM_GC_IN_CURR, bytes)` | 主动 GC |
| `nvdm_query_space_information(part, &info)` | 空间/条目查询 |

返回值：`NVDM_STATUS_OK=0`；`-1` 错误、`-2` 校验和、`-3` 空间不足、
`-4` 未找到、`-5` 参数非法。

最小示例：

```c
nvdm_sim_setup(dev, 0, 16384, 4096, 200);
nvdm_init();
uint8_t buf[] = "hello";
nvdm_write_data_item("app", "key", NVDM_DATA_ITEM_TYPE_RAW_DATA, buf, 5);
uint8_t rb[64]; uint32_t rl = sizeof(rb);
nvdm_read_data_item("app", "key", rb, &rl);   /* rl=5 */
```

## 4. 平台依赖接口规格（移植层 nvdm_sim_port.c）

NVDM 核心库仅通过 `nvdm_port.h` 的 ~20 个契约函数访问平台，移植层逐项实现：

| 契约函数 | 实现方式 |
|----------|----------|
| `nvdm_port_flash_read/write/erase` | 桥接 `flash_sim_read/write/erase`（地址即介质绝对偏移） |
| `nvdm_port_get_peb_address` | `base + pnum*peb_size + offset`，越界 assert |
| `nvdm_port_load_partition_info` | 返回 1 个分区（base=0，peb_count=容量/peb_size） |
| `nvdm_port_get_max_item_cfg` | 返回分区上限（max_item_size=1024，名称长度 16/32） |
| `nvdm_port_malloc/free` | C 库 malloc/free |
| `nvdm_port_mutex_*` / `protect_mutex_*` | 单线程仿真，空实现 |
| `nvdm_port_must_assert` | `assert(0)` |
| `nvdm_port_get_count/duration_time` | `clock_gettime(CLOCK_MONOTONIC)` 微秒 |
| `nvdm_port_get_curr_task_name` | 返回 "main" |
| `nvdm_port_send_queue` / `nvdm_request_gc_in_daemon` | 返回 false（未启用 daemon） |
| `nvdm_port_poweroff*` | 空实现 |

日志：`nvdm_msgid_log.h` 声明的 `nvdm_001~nvdm_133` 字符串符号在移植层定义；
6 个 `nvdm_log_*` 函数输出到 stdout（`[NVDM][I/W/E]` 前缀）。

## 5. 分区与编译配置

- 分区：1 个分区，起始偏移 0；容量须为擦除块整数倍且 >= 2 块（GC 需
  reserved 块）。PEB_SIZE 必须等于 flash 擦除块大小（默认 4KB）。
- 条目上限：`NVDM_ITEM_COUNT`（测试默认 200），决定 RAM 项头镜像大小。
- 编译宏：`MTK_NVDM_ENABLE`（总开关，必须）；不定义 `__EXT_BOOTLOADER__`、
  `SYSTEM_DAEMON_TASK_ENABLE`、`FREERTOS_ENABLE`（保持简单形态）。
- include 路径：`frameworks/nvdm`、`frameworks/nvdm/vendor/inc`、`simulator`。
- 源文件：`vendor/src/nvdm_main.c`、`nvdm_data.c`、`nvdm_io.c` + `nvdm_sim_port.c`。

## 6. 已知限制

- `nvdm_init` 仅允许一次；进程内"掉电重启"需 `nvdm_sim_reset()` 后重新 init。
- 单条数据项上限 1024 字节（配置值，最大 2048）。
- 组名 <=15 字符、项名 <=31 字符（含 `\0`）。
- 依赖块擦除语义，EEPROM 介质不适用（测试程序自动切换为 NOR）。
- 仅编译 vendor 核心三文件；`nvdm_cli.c`/`nvkey.c` 因依赖 minicli/业务 ID 表未纳入。

## 7. 验证方法

组件自检：

```bash
# 默认：全部基础项
cd frameworks/nvdm/test && gcc -DMTK_NVDM_ENABLE -I../../.. -I../../.. \
  -I../vendor/inc main_nvdm.c ../nvdm_sim_port.c ../vendor/src/nvdm_main.c \
  ../vendor/src/nvdm_data.c ../vendor/src/nvdm_io.c ../../../../simulator/flash_sim.c \
  -o main_nvdm && ./main_nvdm

# 功能压测（50 条 32 字节，20 轮 50% 修改）
KV_TESTS=func KV_ITEMS="32,50,50" ./main_nvdm
```

预期：全部测试项 `[OK]`，`STATS_JSON` 输出，func 数据丢失为 0。
