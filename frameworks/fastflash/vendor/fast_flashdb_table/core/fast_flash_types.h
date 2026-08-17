#ifndef FAST_FLASH_TYPES_H
#define FAST_FLASH_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Flash 基本配置
#define FLASH_SECTOR_SIZE         0x1000      // 4KB 扇区大小
#define FLASH_WRITE_CHUNK_SIZE    1024        // 每次写入1KB
#define MAX_TABLES_ALL_SECTOR     24           //最多表数量  这个跟空间利用率有关 建议改小
#define TABLE_NAME_MAX_LEN        32          // 表名最大长度（需 >= 实际表名长度，否则会被截断导致查表失败）
#define MAGIC_NUMBER_TABLE        0x0531      // 表魔数
#define MAGIC_NUMBER_MANAGER      0xAAAA      // 管理表魔数 "AA"
#define MANAGER_TABLE_VERSION     1           // 管理表版本

// 表状态枚举
typedef enum {
    TABLE_STATUS_INVALID = 0,     // 无效
    TABLE_STATUS_VALID   = 1,     // 有效
    TABLE_STATUS_DELETED = 2      // 已删除
} table_status_t;

// 表头结构体
typedef struct __attribute__((packed)) {
    uint16_t magic;               // 魔数，用于版本和有效性检查
    char     name[TABLE_NAME_MAX_LEN]; // 表名，8字符内
    uint32_t table_size;          // 表总大小
    uint32_t data_len;            // 实际数据长度
    uint32_t struct_size;         // 单个结构体大小
    uint32_t struct_nums;         // 结构体数量
    uint32_t data_crc;            // 数据CRC校验
} table_header_t;

// Flash表信息（管理表中存储）
typedef struct __attribute__((packed)) {
    char     name[TABLE_NAME_MAX_LEN]; // 表名
    uint32_t addr;                // 表在Flash中的起始地址
    uint32_t size;                // 表分配的空间大小
    uint32_t used_size;           // 已使用大小
    uint16_t magic;               // 表魔数
    uint8_t  status;              // 表状态
    uint8_t  reserved;            // 保留字段
    uint32_t next_manager_addr;   // 下一个管理表地址（链表）
} flash_table_info_t;

// 管理表结构体
typedef struct __attribute__((packed)) {
    uint16_t magic;                    // 管理表魔数
    uint32_t crc;                      // CRC32校验（放在魔数后面）
    uint8_t  version;                  // 版本号
    uint8_t  table_count;              // 有效表数量
    uint32_t total_size;               // Flash总大小
    uint32_t used_size;                // 已使用大小
    uint32_t next_manager_addr;        // 下一个管理表预留地址
    flash_table_info_t tables[MAX_TABLES_ALL_SECTOR]; // 表信息数组
} flash_manager_table_t;

// 公共表结构（对外API使用）
typedef struct {
    char     name[TABLE_NAME_MAX_LEN];
    uint32_t addr;
    uint32_t size;
    uint32_t used_size;
    uint16_t magic;
    uint8_t  status;
} flash_table_t;

// Flash设备操作接口
typedef struct {
    int (*init)(void);
    int (*read)(uint32_t addr, uint8_t *buf, uint32_t size);
    int (*write)(uint32_t addr, const uint8_t *buf, uint32_t size);
    int (*erase)(uint32_t addr, uint32_t size);
} flash_ops_t;

#ifdef __cplusplus
}
#endif

#endif // FAST_FLASH_TYPES_H