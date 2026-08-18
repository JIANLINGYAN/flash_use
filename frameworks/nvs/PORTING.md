# Zephyr NVS 组件库移植说明（PORTING）

## 1. 组件概述

- 名称：Zephyr NVS（Non-Volatile Storage）
- 来源：Zephyr project `subsys/kvss/nvs/`（Apache-2.0）
- 存储语义类别：**KV（ID+数据）**
- 功能：按 16 位 ID 读写数据、扇区式 ATE 日志、掉电安全、自动垃圾回收、
  空间/条目查询
- 适用介质：NOR/NAND（块擦除、字节写，写入仅允许 1->0）

## 2. 目录结构

```
frameworks/nvs/
├── vendor/                 # 框架本体（零修改）
│   ├── nvs.c nvs_priv.h
│   └── include/zephyr/kvss/nvs.h   # 公共 API 头（原路径）
├── test/main_nvs.c         # 组件自检
└── PORTING.md
```

## 3. 对外 API（vendor/include/zephyr/kvss/nvs.h）

- `nvs_mount(&fs)`：挂载（初始化控制块并扫描恢复）
- `nvs_write(&fs, id, data, len)`：写/更新（len=0 即删除）
- `nvs_read(&fs, id, data, len)`：读（返回实际长度；<0 为错误码）
- `nvs_delete(&fs, id)`：删除
- `nvs_clear(&fs)`：全清
- `nvs_calc_free_space(&fs)`：剩余空间
- `struct nvs_fs`：由调用者填充 `offset/sector_size/sector_count/flash_device`

最小示例：

```c
struct nvs_fs fs;
fs.offset = 0; fs.sector_size = 4096;
fs.sector_count = 4; fs.flash_device = zdev;
nvs_mount(&fs);
uint8_t v[] = "hi";
nvs_write(&fs, 1, v, 2);
uint8_t rb[8]; size_t rl = sizeof(rb);
nvs_read(&fs, 1, rb, &rl);
```

## 4. 平台依赖（经 zephyr_compat 兼容层）

- `flash_get_parameters` / `flash_get_write_block_size` /
  `flash_get_page_info_by_offs` / `flash_read/write/flatten` → 兼容层
- `k_mutex` → 兼容层（单线程空实现）
- `crc8_ccitt` / `crc32_ieee` → 兼容层软件实现
- 日志 `LOG_*` → 兼容层 stdout

## 5. 分区与编译配置

- 1 个分区（offset=0），`sector_count >= 2`；`sector_size` 须为页大小
  （=erase_size）整数倍。
- 编译宏：`CONFIG_FLASH_HAS_EXPLICIT_ERASE`。
- include：`frameworks/nvs/vendor/include` + `frameworks/zephyr_compat/include`。

## 6. 已知限制

- key 为 16 位 ID，适配层用 hash 映射字符串 key。
- 单条数据上限由 `NVS_MAX_SECTOR_SIZE`（64KB）约束，实际受分区容量限制。
- 掉电测试：分区末尾空白区注入残留数据后重新 `nvs_mount` 验证恢复。

## 7. 验证方法

```bash
python3 ../../../scripts/run_tests.py nvs
KV_TESTS=func KV_ITEMS="32,50,50" KV_ROUNDS=20 /tmp/main_nvs
```
