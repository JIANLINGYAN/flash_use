# Zephyr ZMS 组件库移植说明（PORTING）

## 1. 组件概述

- 名称：Zephyr ZMS（Zephyr Memory Storage）
- 来源：Zephyr project `subsys/kvss/zms/`（Apache-2.0）
- 存储语义类别：**KV（ID+数据，固定大小槽位）**
- 功能：按 32 位 ID 读写数据、固定槽位分配、ATE+数据 CRC、磨损均衡、
  掉电安全、垃圾回收（定位替代 NVS 的新引擎）
- 适用介质：NOR/NAND（块擦除、字节写，写入仅允许 1->0）

## 2. 目录结构

```
frameworks/zms/
├── vendor/                 # 框架本体（零修改）
│   ├── zms.c zms_priv.h
│   └── include/zephyr/kvss/zms.h   # 公共 API 头（原路径）
├── test/main_zms.c         # 组件自检
└── PORTING.md
```

## 3. 对外 API（vendor/include/zephyr/kvss/zms.h）

- `zms_mount(&fs)`：挂载（初始化并扫描恢复；失败可 `zms_mount_force` 重建）
- `zms_write(&fs, id, data, len)`：写/更新（len=0 即删除）
- `zms_read(&fs, id, data, len)`：读（返回实际长度；<0 为错误码）
- `zms_delete(&fs, id)`：删除
- `zms_clear(&fs)`：全清
- `zms_get_data_length` / `zms_num_sectors_free` / `zms_calc_free_space`
- `struct zms_fs`：由调用者填充 `offset/sector_size/sector_count/flash_device`

最小示例：

```c
struct zms_fs fs;
fs.offset = 0; fs.sector_size = 4096;
fs.sector_count = 4; fs.flash_device = zdev;
zms_mount(&fs);
uint8_t v[] = "hi";
zms_write(&fs, 1, v, 2);
uint8_t rb[8]; size_t rl = sizeof(rb);
zms_read(&fs, 1, rb, &rl);
```

## 4. 平台依赖（经 zephyr_compat 兼容层）

- `flash_get_parameters` / `flash_get_page_info_by_offs` /
  `flash_read/write/erase` → 兼容层
- `flash_params_get_erase_cap`（返回 FLASH_ERASE_C_EXPLICIT）→ 兼容层
  （要求定义 `CONFIG_FLASH_HAS_EXPLICIT_ERASE`）
- `k_mutex` → 兼容层（单线程空实现）
- `crc8_ccitt` / `crc32_ieee` → 兼容层软件实现
- 日志 `LOG_*` → 兼容层 stdout

## 5. 分区与编译配置

- 1 个分区（offset=0），`sector_count >= 2`；`sector_size` 须为页大小
  （=erase_size）整数倍，且不小于 5 个 ATE 槽位。
- 编译宏：`CONFIG_FLASH_HAS_EXPLICIT_ERASE`。
- include：`frameworks/zms/vendor/include` + `frameworks/zephyr_compat/include`。

## 6. 已知限制

- key 为 32 位 ID，适配层用 hash 映射字符串 key。
- 固定槽位分配，`zms_write` 长度受 ATE 数据区约束（单条受
  `ZMS_MAX_SECTOR_SIZE` 控制）。
- 掉电测试：分区末尾空白区注入残留数据后重新 `zms_mount` 验证恢复。

## 7. 验证方法

```bash
python3 ../../../scripts/run_tests.py zms
KV_TESTS=func KV_ITEMS="32,50,50" KV_ROUNDS=20 /tmp/main_zms
```
