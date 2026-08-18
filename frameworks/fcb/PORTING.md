# Zephyr FCB 组件库移植说明（PORTING）

## 1. 组件概述

- 名称：Zephyr FCB（Flash Circular Buffer）
- 来源：Zephyr project `subsys/fs/fcb/`（Apache-2.0）
- 存储语义类别：**环形日志/事件流**（append-only + 回卷覆盖）
- 功能：顺序追加记录、遍历读取（walk/getnext）、扇区轮转（rotate）、
  清空、掉电恢复（重新扫描）、CRC 校验
- 适用介质：NOR/NAND（块擦除语义）

## 2. 目录结构

```
frameworks/fcb/
├── vendor/                 # 框架本体（零修改）
│   ├── fcb.c fcb_append.c fcb_elem_info.c fcb_getnext.c
│   ├── fcb_rotate.c fcb_walk.c fcb_priv.h
│   └── include/zephyr/fs/fcb.h     # 公共 API 头（原路径）
├── test/main_fcb.c         # 组件自检
└── PORTING.md
```

## 3. 对外 API（vendor/include/zephyr/fs/fcb.h）

- `fcb_init(f_area_id, fcbp)`：初始化（挂载分区，恢复扫描）
- `fcb_append(fcbp, len, &loc)` + `fcb_append_finish`：追加记录
- `fcb_getnext` / `fcb_walk`：顺序遍历
- `fcb_rotate(fcbp)`：轮转（擦除最老扇区）
- `fcb_clear(fcbp)`：清空（循环轮转）
- `fcb_is_empty` / `fcb_offset_last_n` / `fcb_free_space_cnt`
- 数据偏移宏：`FCB_ENTRY_FA_DATA_OFF(entry)`（注意传**值**而非指针）

最小示例：

```c
struct fcb fcb;
struct flash_sector sectors[4];
fcb.f_magic = 0x1234abcd; fcb.f_version = 1;
fcb.f_sector_cnt = 4; fcb.f_scratch_cnt = 1; fcb.f_sectors = sectors;
zephyr_compat_register_flash(dev, 4096, 1, 0xFF);
zephyr_compat_register_area(0, zdev, 0, 16384);
fcb_init(0, &fcb);
struct fcb_entry loc;
fcb_append(&fcb, len, &loc);
flash_area_write(fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), data, len);
fcb_append_finish(&fcb, &loc);
```

## 4. 平台依赖（经 zephyr_compat 兼容层）

- `flash_area_open/read/write/flatten/align/get_sectors/erased_val` → 兼容层
- `flash_get_parameters` → 兼容层（erase_value 等）
- `k_mutex` → 兼容层（单线程空实现）
- `crc8_ccitt` → 兼容层软件实现

## 5. 分区与编译配置

- 分区：1 个（id=0），容量须为擦除块整数倍且 >= 2 块（scratch 1 块）。
- 扇区表由调用者提供，每扇区大小 = 擦除块大小。
- 编译宏：`CONFIG_FLASH_HAS_EXPLICIT_ERASE`。
- 源文件：vendor 下 6 个 fcb_*.c + zephyr_compat.c + flash_sim.c。
- include：`frameworks/fcb/vendor/include`（zephyr/fs/fcb.h）、
  `frameworks/zephyr_compat/include`。

## 6. 已知限制

- FCB 是 append-only，同一数据"覆盖更新"在介质上会累积旧记录，读侧
  需自行取最新（适配器已处理）；高覆盖场景写放大明显。
- `f_scratch_cnt` 须 >= 1（轮转需要 scratch 扇区）。
- 掉电测试：分区末尾空白区注入残留数据后重载，验证已提交记录完好。

## 7. 验证方法

```bash
cd frameworks/fcb/test
python3 ../../../scripts/run_tests.py fcb
# 或直接：
KV_TESTS=append,walk,rotate,clear,powerloss,func KV_ROUNDS=20 /tmp/main_fcb
```
