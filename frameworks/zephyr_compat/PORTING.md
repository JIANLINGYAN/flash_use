# Zephyr 存储组件共享兼容层（PORTING）

## 1. 组件概述

- 名称：Zephyr 存储组件共享兼容层（`frameworks/zephyr_compat`）
- 来源：Zephyr project（Apache-2.0），本目录为**非 Zephyr 环境的桥接模拟层**
- 定位：FCB / NVS / ZMS 三个 Zephyr 存储组件 vendor 零修改，通过本兼容层
  把 Zephyr 抽象（flash 设备 API、flash_area 分区、k_mutex、工具宏、日志、
  CRC）桥接到本平台模拟基座 `simulator/flash_sim.c`。

## 2. 目录结构

```
frameworks/zephyr_compat/
├── include/zephyr/            # 模拟 Zephyr 头文件（供 vendor include）
│   ├── device.h               # struct device（sim 句柄 + params + page_size）
│   ├── kernel.h               # k_mutex（单线程空实现）+ K_FOREVER
│   ├── drivers/flash.h        # flash_parameters + flash 设备 API 声明
│   ├── storage/flash_map.h    # flash_area / flash_sector + 分区 API 声明
│   ├── sys/crc.h              # crc8_ccitt / crc32_ieee 声明
│   ├── sys/util.h             # KB / MIN / MAX / ARRAY_SIZE / GENMASK 等
│   ├── sys/util_macro.h       # BIT / IS_ENABLED / FIELD_GET 等
│   ├── sys/util_internal.h    # IS_ENABLED 展开内部宏
│   ├── sys/__assert.h         # __ASSERT_NO_MSG（空）
│   ├── toolchain.h            # __packed / __unused / BUILD_ASSERT 等
│   ├── types.h                # 基本类型
│   └── logging/log.h          # LOG_*（stdout，LOG_DBG 置空）
├── zephyr_compat.c            # flash/flash_area/k_mutex/CRC 实现（桥接 flash_sim）
├── zephyr_compat.h            # 移植接口：设备/分区注册
└── PORTING.md
```

## 3. 移植接口（zephyr_compat.h）

| 接口 | 说明 |
|------|------|
| `zephyr_compat_register_flash(dev, erase_size, write_block_size, erase_value)` | 注册模拟介质为 Zephyr 设备，返回 `struct device*` |
| `zephyr_compat_register_area(id, zdev, off, size)` | 注册分区（FCB 用；NVS/ZMS 直接用设备） |

## 4. 桥接映射

| Zephyr API | 桥接实现 |
|------------|----------|
| `flash_read/write` | `flash_sim_read/write`（地址=介质绝对偏移） |
| `flash_erase/flatten` | `flash_sim_erase`（按块） |
| `flash_get_parameters` | 设备内 `flash_parameters`（write_block_size/erase_value） |
| `flash_get_write_block_size` | `params->write_block_size` |
| `flash_get_page_info_by_offs` | 页大小=erase_size（模拟层页=扇区） |
| `flash_area_open/read/write/erase/flatten/align/get_sectors/erased_val` | 基于注册分区偏移的 flash_sim 操作 |
| `k_mutex_*` | 单线程仿真空实现 |
| `crc8_ccitt` / `crc32_ieee` | 纯软件实现（poly 0x07 / 0xEDB88320） |

## 5. 编译要求

- 必须定义 `CONFIG_FLASH_HAS_EXPLICIT_ERASE`（本平台介质均需显式擦除，
  保证 ZMS 走 `flash_erase` 分支）。
- include 路径：`frameworks/zephyr_compat/include`（vendor 头依赖链）。
- 不定义 `CONFIG_*_LOOKUP_CACHE` 等（保持最小形态），亦不启用 ZMS 的
  settings 后端。

## 6. 已知限制

- 单分区、单设备模型；`flash_get_page_info` 以 erase_size 作为页大小。
- `LOG_DBG` 置空（规避 64 位 `%llx` 打印差异与刷屏）。
- `k_mutex` 为单线程空实现，不提供真实互斥。
- vendor 编译时会有少量 -Wsign-compare / unused 参数警告，源自上游源码，未修改。

## 7. 验证方法

见三个组件各自 PORTING.md 的验证章节，或统一运行：

```bash
python3 scripts/run_tests.py fcb nvs zms
python3 scripts/check_app_build.py fcb nvs zms
```
