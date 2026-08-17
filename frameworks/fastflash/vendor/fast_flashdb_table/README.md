# Fast Flash Database

一个高效的嵌入式Flash数据库管理系统，专为NOR Flash设备设计，采用紧密排布的管理表链表结构。

## 设计特点

### 核心理念
- **紧密排布**：数据紧密存储，减少空间浪费
- **链表式管理表**：避免单点磨损，延长Flash寿命
- **扇区对齐**：只有表不跨扇区时才对齐，其他时候紧密排布
- **可打断写入**：支持分段写入，保证实时性

### 架构层次
```
应用层API (fast_flash_core.h)
    ↓
核心管理层 (fast_flash_core.c)
    ↓
Flash适配层 (flash_adapter_*.c)
    ↓
底层Flash驱动
```

## 快速开始

### 编译和运行
```bash
# 方法1: 使用Makefile编译
make                    # 编译核心测试
make test               # 运行核心测试
make test-all           # 运行所有测试

# 方法2: 使用CMake编译
mkdir build && cd build
cmake ..
cmake --build .
# 生成可执行文件在build目录下

# 方法3: Windows批处理
compile_tests.bat       # 编译所有测试

# 可执行文件：
# - fast_flash_test.exe (核心数据库测试)
# - health_test.exe (健康管理应用测试)
```

### 基本使用示例
```c
#include "fast_flash_core.h"
#include "flash_adapter_win.h"
#include "fast_flash_log.h"

// 1. 初始化系统
flash_log_set_level(LOG_LEVEL_DEBUG);
fast_flash_init(&win_flash_ops, 64*1024, false);

// 2. 创建表
typedef struct {
    uint32_t id;
    char name[16];
    float value;
} my_data_t;

fast_flash_create_table("MYTABLE", sizeof(my_data_t), 10);

// 3. 写入数据（自动追加，无需管理偏移量）
my_data_t data = {1, "Test", 3.14f};
fast_flash_write_table_data("MYTABLE", &data, sizeof(data));

// 4. 读取数据（按序号读取）
my_data_t read_data;
fast_flash_read_table_data("MYTABLE", 0, &read_data, sizeof(read_data));
```

## 核心概念

### 表结构
每个表都包含固定的表头：
- `magic`: 魔数，用于版本和有效性检查
- `name`: 表名（最大8字符）
- `table_size`: 表总大小
- `data_len`: 实际数据长度
- `struct_size`: 单个结构体大小
- `struct_nums`: 结构体数量
- `data_crc`: 数据CRC校验

### 管理表链表
- 首次初始化时创建第一个管理表
- 每次更新时写入预留位置，并预留下一个位置
- 启动时遍历链表找到最新有效管理表
- 紧密排布，最小化空间浪费

### 空间管理策略
1. **表创建**：紧密排布，只在需要时扇区对齐
2. **表删除**：软删除，标记为无效
3. **垃圾回收**：只在空间不足时执行，整理碎片
4. **磨损均衡**：管理表链表分散擦除次数

## API参考

### 初始化
```c
int fast_flash_init(const flash_ops_t *ops, uint32_t total_size, bool allow_erase);
```

### 表管理
```c
int fast_flash_create_table(const char *name, uint32_t struct_size, uint32_t max_structs);
int fast_flash_delete_table(const char *name);
int fast_flash_write_table_data(const char *table_name, const void *data, uint32_t size);
int fast_flash_read_table_data(const char *table_name, uint32_t index, void *buffer, uint32_t size);
int fast_flash_get_table_info(const char *table_name, flash_table_t *info);
```

### 管理功能
```c
int fast_flash_list_tables(flash_table_t *tables, int max_count);
bool fast_flash_table_exists(const char *name);
int fast_flash_gc(void);
void fast_flash_set_erase_allowed(bool allowed);
```

### 调试功能
```c
void fast_flash_dump_manager_table(void);
uint32_t fast_flash_get_free_size(void);
int fast_flash_validate_table_data(const char *table_name);
```

## 移植指南

### 创建平台适配层
实现 `flash_ops_t` 接口：
```c
struct fal_flash_dev {
    int (*init)(void);
    int (*read)(uint32_t addr, uint8_t *buf, uint32_t size);
    int (*write)(uint32_t addr, const uint8_t *buf, uint32_t size);
    int (*erase)(uint32_t addr, uint32_t size);
};
```

### 平台特定注意事项
1. **NOR Flash特性**：只能将1写成0，擦除前需要先擦除
2. **写入对齐**：遵循设备的写入粒度要求
3. **擦除大小**：按设备的擦除块大小对齐
4. **中断处理**：确保写入过程可被打断

## 性能特性

- **低延迟**：分段写入，支持实时系统
- **高可靠性**：CRC校验，数据完整性保护
- **长寿命**：磨损均衡，减少擦除次数
- **空间效率**：紧密排布，最小化碎片

## 文件结构
```
fast_flash/
├── core/                   # 核心代码目录
│   ├── fast_flash_types.h  # 数据类型定义
│   ├── fast_flash_core.h   # 核心API接口
│   ├── fast_flash_core.c   # 核心实现
│   ├── fast_flash_log.h    # 日志系统
│   └── fast_flash_log.c    # 日志实现
├── port_win/              # Windows平台适配
│   ├── flash_adapter_win.h # Windows适配层接口
│   ├── flash_adapter_win.c # Windows模拟实现
│   └── test_fast_flash.c   # 核心库测试套件
├── app/                   # 应用层代码
│   ├── health_data_manager.h # 健康数据管理API
│   └── health_data_manager.c # 健康数据管理实现
├── health_tests/           # 应用层测试
│   └── test_health_manager.c  # 健康管理测试套件
├── Makefile               # Make编译脚本
├── CMakeLists.txt         # CMake配置
├── compile_tests.bat      # Windows编译脚本
└── README.md              # 本文档
```