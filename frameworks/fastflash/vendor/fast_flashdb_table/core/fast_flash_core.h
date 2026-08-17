#ifndef FAST_FLASH_CORE_H
#define FAST_FLASH_CORE_H

#include "fast_flash_types.h"

#ifdef __cplusplus
extern "C" {
#endif

    // 核心初始化函数
    int fast_flash_init(const flash_ops_t *ops, uint32_t total_size, bool allow_erase);

    // 表管理函数
    int fast_flash_create_table(const char *name, uint32_t struct_size, uint32_t max_structs);
    int fast_flash_delete_table(const char *name);
    int fast_flash_write_table_data(const char *table_name, const void *data, uint32_t size);
    int fast_flash_read_table_data(const char *table_name, uint32_t index, void *buffer, uint32_t size);
    int fast_flash_get_table_info(const char *table_name, flash_table_t *info);

    // 表数据管理函数
    uint32_t fast_flash_get_table_count(const char *table_name);  // 获取当前表写入的数据数量
    int fast_flash_write_table_data_by_index(const char *table_name, uint32_t index, const void *data, uint32_t size);  // 写入指定位置
    int fast_flash_append_table_data(const char *table_name, const void *data, uint32_t size);  // 累加数据，基于max_structs管控
    int fast_flash_clear_table_data(const char *table_name, uint64_t clear_mask);  // 清除指定mask标记的数据，保证索引连续
    int fast_flash_write_table_data_batch(const char *table_name, const void *data, uint32_t struct_size, uint32_t count);  // 批量写入数据，避免频繁构建新表

    // 表查询函数
    int fast_flash_list_tables(flash_table_t *tables, int max_count);
    bool fast_flash_table_exists(const char *name);

    // 管理函数
    void fast_flash_set_erase_allowed(bool allowed);
    bool fast_flash_is_erase_allowed(void);
    int fast_flash_gc(void);  // 垃圾回收

    // 调试和状态函数
    void fast_flash_dump_manager_table(void);
    uint32_t fast_flash_get_total_size(void);
    uint32_t fast_flash_get_used_size(void);
    uint32_t fast_flash_get_free_size(void);

    // 实用函数
    int fast_flash_validate_table_data(const char *table_name);
    int fast_flash_repair_table(const char *table_name);

#ifdef __cplusplus
}
#endif

#endif // FAST_FLASH_CORE_H