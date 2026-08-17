#include "../core/fast_flash_core.h"
#include "flash_adapter_win.h"
#include "../core/fast_flash_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 测试用的数据结构
typedef struct {
    uint32_t id;
    char name[16];
    float value;
    bool active;
} test_data_t;

typedef struct {
    uint32_t timestamp;
    float temperature;
    uint16_t humidity;
    uint8_t status;
} sensor_data_t;

void print_test_data(const test_data_t *data) {
    printf("ID: %u, Name: %-16s Value: %.2f, Active: %s\n", 
           data->id, data->name, data->value, data->active ? "Yes" : "No");
}

void print_sensor_data(const sensor_data_t *data) {
    printf("Time: %u, Temp: %.1f °C, Humidity: %u%%, Status: %u\n",
           data->timestamp, data->temperature, data->humidity, data->status);
}

int test_basic_operations(void) {
    printf("\n=== Testing Basic Operations ===\n");
    
    // 创建测试表
    if (fast_flash_create_table("TEST", sizeof(test_data_t), 10) != 0) {
        printf("Failed to create TEST table\n");
        return -1;
    }
    
    // 写入测试数据
    test_data_t test_items[] = {
        {1, "Item1", 1.23f, true},
        {2, "Item2", 4.56f, false},
        {3, "Item3", 7.89f, true}
    };
    
    for (int i = 0; i < 3; i++) {
        if (fast_flash_write_table_data("TEST", &test_items[i], sizeof(test_data_t)) != 0) {
            printf("Failed to write test item %d\n", i);
            return -1;
        }
    }
    
    // 读取并验证数据
    for (int i = 0; i < 3; i++) {
        test_data_t read_item;
        if (fast_flash_read_table_data("TEST", i, &read_item, sizeof(test_data_t)) != 0) {
            printf("Failed to read test item %d\n", i);
            return -1;
        }
        
        printf("Read item %d: ", i);
        print_test_data(&read_item);
        
        // 验证数据
        if (read_item.id != test_items[i].id || 
            strcmp(read_item.name, test_items[i].name) != 0 ||
            read_item.value != test_items[i].value ||
            read_item.active != test_items[i].active) {
            printf("Data mismatch for item %d\n", i);
            return -1;
        }
    }
    
    // 获取表信息
    flash_table_t table_info;
    if (fast_flash_get_table_info("TEST", &table_info) == 0) {
        printf("Table info: Name=%s, Addr=0x%08X, Size=%u, Used=%u, Status=%u\n",
               table_info.name, table_info.addr, table_info.size, table_info.used_size, table_info.status);
    }
    
    // 验证表数据
    if (fast_flash_validate_table_data("TEST") != 0) {
        printf("Table validation failed\n");
        return -1;
    }
    printf("Table validation passed\n");
    
    printf("Basic operations test passed!\n");
    return 0;
}

int test_multiple_tables(void) {
    printf("\n=== Testing Multiple Tables ===\n");
    
    // 创建多个表
    if (fast_flash_create_table("SENSOR", sizeof(sensor_data_t), 20) != 0) {
        printf("Failed to create SENSOR table\n");
        return -1;
    }
    
    if (fast_flash_create_table("CONFIG", sizeof(uint32_t), 5) != 0) {
        printf("Failed to create CONFIG table\n");
        return -1;
    }
    
    // 写入传感器数据
    sensor_data_t sensor_items[] = {
        {1000, 25.5f, 60, 1},
        {1001, 26.0f, 65, 1},
        {1002, 24.8f, 58, 0}
    };
    
    for (int i = 0; i < 3; i++) {
        fast_flash_write_table_data("SENSOR", &sensor_items[i], sizeof(sensor_data_t));
    }
    
    // 写入配置数据
    uint32_t config_values[] = {100, 200, 300, 400, 500};
    for (int i = 0; i < 5; i++) {
        fast_flash_write_table_data("CONFIG", &config_values[i], sizeof(uint32_t));
    }
    
    // 列出所有表
    flash_table_t tables[10];
    int table_count = fast_flash_list_tables(tables, 10);
    printf("Total tables: %d\n", table_count);
    for (int i = 0; i < table_count; i++) {
        printf("  [%d] Name: %-8s Size: %5u Used: %5u\n", 
               i, tables[i].name, tables[i].size, tables[i].used_size);
    }
    
    // 验证所有表
    if (fast_flash_validate_table_data("SENSOR") != 0) {
        printf("SENSOR table validation failed\n");
        return -1;
    }
    
    if (fast_flash_validate_table_data("CONFIG") != 0) {
        printf("CONFIG table validation failed\n");
        return -1;
    }
    
    printf("Multiple tables test passed!\n");
    return 0;
}

int test_table_deletion(void) {
    printf("\n=== Testing Table Deletion ===\n");
    
    // 删除一个表
    if (fast_flash_delete_table("CONFIG") != 0) {
        printf("Failed to delete CONFIG table\n");
        return -1;
    }
    
    // 检查表是否被删除
    if (fast_flash_table_exists("CONFIG")) {
        printf("CONFIG table still exists after deletion\n");
        return -1;
    }
    
    // 检查其他表是否仍然存在
    if (!fast_flash_table_exists("TEST") || !fast_flash_table_exists("SENSOR")) {
        printf("Other tables were affected by deletion\n");
        return -1;
    }
    
    // 尝试读取已删除的表
    uint32_t buffer;
    if (fast_flash_read_table_data("CONFIG", 0, &buffer, sizeof(buffer)) == 0) {
        printf("Should not be able to read deleted table\n");
        return -1;
    }
    
    printf("Table deletion test passed!\n");
    return 0;
}

int test_persistence(void) {
    printf("\n=== Testing Persistence ===\n");
    
    // 保存当前状态用于验证
    uint32_t original_used_size = fast_flash_get_used_size();
    
    // 重新初始化（模拟重启）
    if (fast_flash_init(&win_flash_ops, WIN_FLASH_TOTAL_SIZE, false) != 0) {
        printf("Failed to reinitialize flash\n");
        return -1;
    }
    
    // 检查表是否仍然存在
    if (!fast_flash_table_exists("TEST") || !fast_flash_table_exists("SENSOR")) {
        printf("Tables lost after reinitialization\n");
        return -1;
    }
    
    // 验证数据完整性
    if (fast_flash_validate_table_data("TEST") != 0) {
        printf("TEST table data corrupted after restart\n");
        return -1;
    }
    
    if (fast_flash_validate_table_data("SENSOR") != 0) {
        printf("SENSOR table data corrupted after restart\n");
        return -1;
    }
    
    // 检查使用空间是否一致
    uint32_t new_used_size = fast_flash_get_used_size();
    if (new_used_size != original_used_size) {
        printf("Used size mismatch after restart: original=%u, new=%u\n", 
               original_used_size, new_used_size);
        return -1;
    }
    
    // 读取并验证之前的数据
    test_data_t test_item;
    if (fast_flash_read_table_data("TEST", 0, &test_item, sizeof(test_item)) == 0) {
        printf("Read TEST data after restart: ");
        print_test_data(&test_item);
        
        if (test_item.id != 1 || strcmp(test_item.name, "Item1") != 0) {
            printf("TEST data corrupted after restart\n");
            return -1;
        }
    }
    
    sensor_data_t sensor_item;
    if (fast_flash_read_table_data("SENSOR", 0, &sensor_item, sizeof(sensor_item)) == 0) {
        printf("Read SENSOR data after restart: ");
        print_sensor_data(&sensor_item);
        
        if (sensor_item.timestamp != 1000 || sensor_item.temperature != 25.5f) {
            printf("SENSOR data corrupted after restart\n");
            return -1;
        }
    }
    
    printf("Persistence test passed!\n");
    return 0;
}

int test_garbage_collection(void) {
    printf("\n=== Testing Garbage Collection ===\n");
    
    // 显示当前状态
    printf("Before GC:\n");
    fast_flash_dump_manager_table();
    printf("Free space: %u bytes\n", fast_flash_get_free_size());
    
    // 允许擦除操作
    fast_flash_set_erase_allowed(true);
    
    // 执行垃圾回收
    if (fast_flash_gc() != 0) {
        printf("Garbage collection failed\n");
        return -1;
    }
    
    // 显示GC后状态
    printf("\nAfter GC:\n");
    fast_flash_dump_manager_table();
    printf("Free space: %u bytes\n", fast_flash_get_free_size());
    
    // 验证数据完整性
    if (fast_flash_validate_table_data("TEST") != 0) {
        printf("TEST table corrupted after GC\n");
        return -1;
    }
    
    if (fast_flash_validate_table_data("SENSOR") != 0) {
        printf("SENSOR table corrupted after GC\n");
        return -1;
    }
    
    printf("Garbage collection test passed!\n");
    return 0;
}

int test_space_management(void) {
    printf("\n=== Testing Space Management ===\n");
    
    // 创建多个表来测试空间分配
    char table_name[16];
    for (int i = 0; i < 5; i++) {
        snprintf(table_name, sizeof(table_name), "SPACE%u", i);
        if (fast_flash_create_table(table_name, sizeof(uint32_t) * 10, 1) != 0) {
            printf("Failed to create table %s\n", table_name);
            return -1;
        }
    }
    
    // 检查空间使用情况
    printf("After creating more tables:\n");
    fast_flash_dump_manager_table();
    printf("Total: %u, Used: %u, Free: %u\n", 
           fast_flash_get_total_size(), fast_flash_get_used_size(), fast_flash_get_free_size());
    
    printf("Space management test passed!\n");
    return 0;
}

int test_new_table_management_functions(void) {
    printf("\n=== Testing New Table Management Functions ===\n");
    
    // 创建一个专门测试的表，最大容量为5
    if (fast_flash_create_table("MGRTEST", sizeof(test_data_t), 5) != 0) {
        printf("Failed to create MGRTEST table\n");
        return -1;
    }
    
    // 测试1：获取初始数据数量
    uint32_t count = fast_flash_get_table_count("MGRTEST");
    printf("Initial data count: %u\n", count);
    if (count != 0) {
        printf("Expected 0, got %u\n", count);
        return -1;
    }
    
    // 测试2：先用累加功能添加一些数据
    test_data_t initial_data[] = {
        {100, "InitialData1", 12.34f, true},
        {200, "InitialData2", 56.78f, false},
        {300, "InitialData3", 99.99f, true}
    };
    
    for (int i = 0; i < 3; i++) {
        if (fast_flash_append_table_data("MGRTEST", &initial_data[i], sizeof(test_data_t)) != 0) {
            printf("Failed to append initial data %d\n", i);
            return -1;
        }
    }
    
    // 检查数据数量
    count = fast_flash_get_table_count("MGRTEST");
    printf("Data count after initial appends: %u\n", count);
    if (count != 3) {
        printf("Expected 3, got %u\n", count);
        return -1;
    }
    
    // 验证初始数据
    test_data_t read_data;
    for (int i = 0; i < 3; i++) {
        if (fast_flash_read_table_data("MGRTEST", i, &read_data, sizeof(test_data_t)) != 0) {
            printf("Failed to read initial data at index %d\n", i);
            return -1;
        }
        printf("Initial data at index %d: ", i);
        print_test_data(&read_data);
        if (read_data.id != initial_data[i].id || strcmp(read_data.name, initial_data[i].name) != 0) {
            printf("Data mismatch at index %d\n", i);
            return -1;
        }
    }
    
    // 测试3：修改指定index的数据（这是函数的主要用途）
    test_data_t modified_data[] = {
        {150, "ModifiedData1", 11.11f, false},  // 修改index 0
        {250, "ModifiedData2", 22.22f, true}    // 修改index 1
    };
    
    // 修改index 0的数据
    if (fast_flash_write_table_data_by_index("MGRTEST", 0, &modified_data[0], sizeof(test_data_t)) != 0) {
        printf("Failed to modify data at index 0\n");
        return -1;
    }
    
    // 修改index 1的数据
    if (fast_flash_write_table_data_by_index("MGRTEST", 1, &modified_data[1], sizeof(test_data_t)) != 0) {
        printf("Failed to modify data at index 1\n");
        return -1;
    }
    
    // 验证修改后的数据
    if (fast_flash_read_table_data("MGRTEST", 0, &read_data, sizeof(test_data_t)) != 0) {
        printf("Failed to read modified data at index 0\n");
        return -1;
    }
    printf("Modified data at index 0: ");
    print_test_data(&read_data);
    if (read_data.id != 150 || strcmp(read_data.name, "ModifiedData1") != 0) {
        printf("Data mismatch after modification at index 0\n");
        return -1;
    }
    
    if (fast_flash_read_table_data("MGRTEST", 1, &read_data, sizeof(test_data_t)) != 0) {
        printf("Failed to read modified data at index 1\n");
        return -1;
    }
    printf("Modified data at index 1: ");
    print_test_data(&read_data);
    if (read_data.id != 250 || strcmp(read_data.name, "ModifiedData2") != 0) {
        printf("Data mismatch after modification at index 1\n");
        return -1;
    }
    
    // 验证index 2的数据没有被修改
    if (fast_flash_read_table_data("MGRTEST", 2, &read_data, sizeof(test_data_t)) != 0) {
        printf("Failed to read data at index 2\n");
        return -1;
    }
    printf("Unchanged data at index 2: ");
    print_test_data(&read_data);
    if (read_data.id != 300 || strcmp(read_data.name, "InitialData3") != 0) {
        printf("Data should not have changed at index 2\n");
        return -1;
    }
    
    // 检查数据数量没有变化（修改不应该增加数据数量）
    count = fast_flash_get_table_count("MGRTEST");
    printf("Data count after modifications: %u\n", count);
    if (count != 3) {
        printf("Expected 3 (no change), got %u\n", count);
        return -1;
    }
    
    // 测试4：尝试修改不存在的位置
    test_data_t invalid_data = {999, "Invalid", 999.99f, true};
    int result = fast_flash_write_table_data_by_index("MGRTEST", 5, &invalid_data, sizeof(test_data_t));
    if (result != -2) {  // 期望返回超出范围错误
        printf("Expected out of range error (-2), got %d\n", result);
        return -1;
    }
    printf("Out of range modification test passed, correctly returned error %d\n", result);

    // 测试5：继续添加数据直到表满
    test_data_t additional_data[] = {
        {400, "AdditionalData1", 33.33f, false},
        {500, "AdditionalData2", 44.44f, true}
    };
    
    // 添加第四个数据
    if (fast_flash_append_table_data("MGRTEST", &additional_data[0], sizeof(test_data_t)) != 0) {
        printf("Failed to append additional data 1\n");
        return -1;
    }
    
    // 添加第五个数据（表满）
    if (fast_flash_append_table_data("MGRTEST", &additional_data[1], sizeof(test_data_t)) != 0) {
        printf("Failed to append additional data 2\n");
        return -1;
    }
    
    // 检查最终数据数量
    count = fast_flash_get_table_count("MGRTEST");
    printf("Final data count: %u\n", count);
    if (count != 5) {
        printf("Expected 5 (table full), got %u\n", count);
        return -1;
    }
    
    // 测试6：表满后尝试添加更多数据
    test_data_t overflow_data = {600, "Overflow", 55.55f, false};
    result = fast_flash_append_table_data("MGRTEST", &overflow_data, sizeof(test_data_t));
    if (result != -2) {  // 期望返回表满错误
        printf("Expected table full error (-2), got %d\n", result);
        return -1;
    }
    printf("Table full append test passed, correctly returned error %d\n", result);

    // 测试6：覆盖写入（重写已存在的位置）
    test_data_t overwrite_data = {600, "Overwritten", 222.22f, false};
    if (fast_flash_write_table_data_by_index("MGRTEST", 1, &overwrite_data, sizeof(test_data_t)) != 0) {
        printf("Failed to overwrite data at index 1\n");
        return -1;
    }

    // 验证覆盖的数据
    if (fast_flash_read_table_data("MGRTEST", 1, &read_data, sizeof(test_data_t)) != 0) {
        printf("Failed to read overwritten data at index 1\n");
        return -1;
    }
    printf("Overwritten data at index 1: ");
    print_test_data(&read_data);
    if (read_data.id != 600 || strcmp(read_data.name, "Overwritten") != 0) {
        printf("Data mismatch after overwrite at index 1\n");
        return -1;
    }
    
    // 测试7：优化测试 - 写入相同数据时应该直接返回成功
    printf("\n--- Test 7: Optimized write with identical data ---\n");
    
    // 首先读取当前index 1的数据，然后写入完全相同的数据
    test_data_t current_data;
    if (fast_flash_read_table_data("MGRTEST", 1, &current_data, sizeof(test_data_t)) != 0) {
        printf("Failed to read current data at index 1\n");
        return -1;
    }
    printf("Current data at index 1: ");
    print_test_data(&current_data);
    
    // 记录当前写入操作次数
    win_flash_perf_stats_t stats_before;
    win_flash_get_perf_stats(&stats_before);
    printf("Write operations before identical write: %u\n", stats_before.write_operations);
    
    // 尝试写入与当前数据完全相同的数据
    result = fast_flash_write_table_data_by_index("MGRTEST", 1, &current_data, sizeof(test_data_t));
    if (result != 0) {
        printf("Expected success (0) for identical data write, got %d\n", result);
        return -1;
    }
    
    // 检查写入操作次数是否没有增加（说明优化生效）
    win_flash_perf_stats_t stats_after;
    win_flash_get_perf_stats(&stats_after);
    printf("Write operations after identical write: %u\n", stats_after.write_operations);
    
    if (stats_after.write_operations != stats_before.write_operations) {
        printf("Write operations increased from %u to %u, optimization failed\n", 
               stats_before.write_operations, stats_after.write_operations);
        printf("This indicates that the identical data check did not work\n");
        return -1;
    }
    
    printf("Identical data write optimization test passed!\n");
    printf("Write operations remained at %u (no unnecessary writes)\n", stats_before.write_operations);
    
    // 测试8：写入不同数据时应该正常执行
    printf("\n--- Test 8: Normal write with different data ---\n");
    
    win_flash_get_perf_stats(&stats_before);
    
    test_data_t different_data = {700, "DifferentData", 333.33f, true};
    result = fast_flash_write_table_data_by_index("MGRTEST", 1, &different_data, sizeof(test_data_t));
    if (result != 0) {
        printf("Failed to write different data, got %d\n", result);
        return -1;
    }
    
    win_flash_get_perf_stats(&stats_after);
    
    if (stats_after.write_operations <= stats_before.write_operations) {
        printf("Write operations did not increase for different data, got %u\n", 
               stats_after.write_operations);
        return -1;
    }
    
    printf("Different data write test passed!\n");
    printf("Write operations increased from %u to %u (normal write executed)\n", 
           stats_before.write_operations, stats_after.write_operations);
    
    // 验证不同数据写入成功
    if (fast_flash_read_table_data("MGRTEST", 1, &read_data, sizeof(test_data_t)) != 0) {
        printf("Failed to read different data after write\n");
        return -1;
    }
    printf("Different data at index 1: ");
    print_test_data(&read_data);
    if (read_data.id != 700 || strcmp(read_data.name, "DifferentData") != 0) {
        printf("Different data write failed\n");
        return -1;
    }
    
    // 验证表数据完整性
    if (fast_flash_validate_table_data("MGRTEST") != 0) {
        printf("MGRTEST table validation failed\n");
        return -1;
    }
    
    // 显示最终表信息
    flash_table_t table_info;
    if (fast_flash_get_table_info("MGRTEST", &table_info) == 0) {
        printf("MGRTEST table info: Name=%s, Size=%u, Used=%u\n",
               table_info.name, table_info.size, table_info.used_size);
    }
    
    printf("New table management functions test passed!\n");
    return 0;
}

int test_clear_table_data_function(void) {
    printf("\n=== Testing Clear Table Data Function ===\n");
    
    // 创建一个专门测试清除功能的表
    if (fast_flash_create_table("CLEART", sizeof(test_data_t), 10) != 0) {
        printf("Failed to create CLEART table\n");
        return -1;
    }
    
    // 添加一些测试数据
    test_data_t clear_test_data[] = {
        {10, "ClearData1", 1.11f, true},
        {20, "ClearData2", 2.22f, false},
        {30, "ClearData3", 3.33f, true},
        {40, "ClearData4", 4.44f, false},
        {50, "ClearData5", 5.55f, true}
    };
    
    for (int i = 0; i < 5; i++) {
        if (fast_flash_write_table_data("CLEART", &clear_test_data[i], sizeof(test_data_t)) != 0) {
            printf("Failed to write clear test data %d\n", i);
            return -1;
        }
    }
    
    // 检查初始数据数量
    uint32_t initial_count = fast_flash_get_table_count("CLEART");
    printf("Initial data count: %u\n", initial_count);
    if (initial_count != 5) {
        printf("Expected 5, got %u\n", initial_count);
        return -1;
    }
    
    // 测试1：清除指定索引的数据（使用位掩码）
    printf("\n--- Test 1: Clear data using bitmask ---\n");
    
    // 清除index 1和3的数据（位掩码：0b1010，即第1位和第3位为1）
    uint64_t clear_mask = (1ULL << 1) | (1ULL << 3);  // 清除索引1和3
    int result = fast_flash_clear_table_data("CLEART", clear_mask);
    if (result != 0) {
        printf("Failed to clear data with mask 0x%016llX\n", clear_mask);
        return -1;
    }
    
    // 检查数据数量（应该减少）
    uint32_t cleared_count = fast_flash_get_table_count("CLEART");
    printf("Data count after clear: %u\n", cleared_count);
    if (cleared_count != 3) {  // 清除了index 1和3，剩下index 0, 2, 4
        printf("Expected 3, got %u\n", cleared_count);
        return -1;
    }
    
    // 验证剩余的数据（索引应该是连续的）
    test_data_t read_data;
    uint32_t expected_ids[] = {10, 30, 50};  // 清除后剩下的数据ID
    
    for (int i = 0; i < 3; i++) {
        if (fast_flash_read_table_data("CLEART", i, &read_data, sizeof(test_data_t)) != 0) {
            printf("Failed to read remaining data at index %d\n", i);
            return -1;
        }
        printf("Remaining data at index %d: ", i);
        print_test_data(&read_data);
        
        // 检查数据是否正确（清除后索引应该是连续的）
        if (read_data.id != expected_ids[i]) {
            printf("Data mismatch at index %d, expected ID %u, got %u\n", 
                   i, expected_ids[i], read_data.id);
            return -1;
        }
    }
    
    // 测试2：批量清除多个索引
    printf("\n--- Test 2: Batch clear multiple indices ---\n");
    
    // 创建一个新表进行批量测试
    if (fast_flash_create_table("BATCHTE", sizeof(test_data_t), 8) != 0) {
        printf("Failed to create BATCHTE table\n");
        return -1;
    }
    
    // 添加批量测试数据
    test_data_t batch_test_data[] = {
        {100, "Batch1", 10.1f, true},
        {200, "Batch2", 20.2f, false},
        {300, "Batch3", 30.3f, true},
        {400, "Batch4", 40.4f, false},
        {500, "Batch5", 50.5f, true},
        {600, "Batch6", 60.6f, false},
        {700, "Batch7", 70.7f, true},
        {800, "Batch8", 80.8f, false}
    };
    
    for (int i = 0; i < 8; i++) {
        if (fast_flash_write_table_data("BATCHTE", &batch_test_data[i], sizeof(test_data_t)) != 0) {
            printf("Failed to write batch test data %d\n", i);
            return -1;
        }
    }
    
    // 批量清除奇数index的数据（1, 3, 5, 7）
    uint64_t batch_clear_mask = (1ULL << 1) | (1ULL << 3) | (1ULL << 5) | (1ULL << 7);
    if (fast_flash_clear_table_data("BATCHTE", batch_clear_mask) != 0) {
        printf("Failed to perform batch clear\n");
        return -1;
    }
    
    // 检查清除后的数据数量
    uint32_t batch_cleared_count = fast_flash_get_table_count("BATCHTE");
    printf("Batch data count after clear: %u\n", batch_cleared_count);
    if (batch_cleared_count != 4) {  // 只剩下偶数index的数据
        printf("Expected 4, got %u\n", batch_cleared_count);
        return -1;
    }
    
    // 验证剩余的数据
    uint32_t expected_remaining[] = {0, 2, 4, 6};  // 偶数index的数据
    for (int i = 0; i < 4; i++) {
        if (fast_flash_read_table_data("BATCHTE", i, &read_data, sizeof(test_data_t)) != 0) {
            printf("Failed to read remaining batch data at index %d\n", i);
            return -1;
        }
        printf("Remaining batch data at index %d: ", i);
        print_test_data(&read_data);
        
        uint32_t original_index = expected_remaining[i];
        if (read_data.id != batch_test_data[original_index].id || 
            strcmp(read_data.name, batch_test_data[original_index].name) != 0) {
            printf("Batch data mismatch at index %d\n", i);
            return -1;
        }
    }
    
    // 测试3：边界条件测试
    printf("\n--- Test 3: Boundary conditions ---\n");
    
    // 尝试清除不存在的index（超出范围）
    uint64_t invalid_mask = (1ULL << 10);  // 尝试清除第10个index（不存在）
    result = fast_flash_clear_table_data("BATCHTE", invalid_mask);
    if (result != -2) {  // 期望返回超出范围错误
        printf("Expected out of range error (-2) for mask 0x%016llX, got %d\n", invalid_mask, result);
        return -1;
    }
    printf("Out of range clear test passed, correctly returned error %d\n", result);
    
    // 尝试清除掩码为0（应该成功但无操作）
    result = fast_flash_clear_table_data("BATCHTE", 0);
    if (result != 0) {
        printf("Expected success for clear mask 0, got %d\n", result);
        return -1;
    }
    printf("Zero mask clear test passed, correctly returned success\n");
    
    // 验证表数据完整性
    if (fast_flash_validate_table_data("CLEART") != 0) {
        printf("CLEART table validation failed\n");
        return -1;
    }
    
    if (fast_flash_validate_table_data("BATCHTE") != 0) {
        printf("BATCHTE table validation failed\n");
        return -1;
    }
    
    printf("Clear table data function test passed!\n");
    return 0;
}

int test_batch_write_function(void) {
    printf("\n=== Testing Batch Write Function ===\n");
    
    // 创建一个专门测试批量写入的表
    if (fast_flash_create_table("BATCHWR", sizeof(test_data_t), 20) != 0) {
        printf("Failed to create BATCHWR table\n");
        return -1;
    }
    
    // 测试1：单次批量写入多个数据
    printf("\n--- Test 1: Single batch write ---\n");
    
    // 准备批量数据
    test_data_t batch_data1[] = {
        {1000, "BatchItem1", 11.11f, true},
        {1001, "BatchItem2", 22.22f, false},
        {1002, "BatchItem3", 33.33f, true},
        {1003, "BatchItem4", 44.44f, false},
        {1004, "BatchItem5", 55.55f, true}
    };
    
    // 一次性写入5条数据
    int result = fast_flash_write_table_data_batch("BATCHWR", batch_data1, sizeof(test_data_t), 5);
    if (result != 0) {
        printf("Failed to perform batch write (5 items)\n");
        return -1;
    }
    
    // 检查数据数量
    uint32_t count = fast_flash_get_table_count("BATCHWR");
    printf("Data count after batch write: %u\n", count);
    if (count != 5) {
        printf("Expected 5, got %u\n", count);
        return -1;
    }
    
    // 验证所有写入的数据
    test_data_t read_data;
    for (int i = 0; i < 5; i++) {
        if (fast_flash_read_table_data("BATCHWR", i, &read_data, sizeof(test_data_t)) != 0) {
            printf("Failed to read batch data at index %d\n", i);
            return -1;
        }
        printf("Batch data at index %d: ", i);
        print_test_data(&read_data);
        
        if (read_data.id != batch_data1[i].id || 
            strcmp(read_data.name, batch_data1[i].name) != 0 ||
            read_data.value != batch_data1[i].value) {
            printf("Batch data mismatch at index %d\n", i);
            return -1;
        }
    }
    
    // 测试2：多次批量写入（测试空间管理）
    printf("\n--- Test 2: Multiple batch writes ---\n");
    
    // 第二次批量写入
    test_data_t batch_data2[] = {
        {2000, "BatchItem6", 66.66f, false},
        {2001, "BatchItem7", 77.77f, true},
        {2002, "BatchItem8", 88.88f, false}
    };
    
    result = fast_flash_write_table_data_batch("BATCHWR", batch_data2, sizeof(test_data_t), 3);
    if (result != 0) {
        printf("Failed to perform second batch write (3 items)\n");
        return -1;
    }
    
    // 检查数据数量
    count = fast_flash_get_table_count("BATCHWR");
    printf("Data count after second batch write: %u\n", count);
    if (count != 8) {
        printf("Expected 8, got %u\n", count);
        return -1;
    }
    
    // 验证第二次写入的数据
    for (int i = 5; i < 8; i++) {
        if (fast_flash_read_table_data("BATCHWR", i, &read_data, sizeof(test_data_t)) != 0) {
            printf("Failed to read second batch data at index %d\n", i);
            return -1;
        }
        printf("Second batch data at index %d: ", i);
        print_test_data(&read_data);
        
        if (read_data.id != batch_data2[i-5].id || 
            strcmp(read_data.name, batch_data2[i-5].name) != 0) {
            printf("Second batch data mismatch at index %d\n", i);
            return -1;
        }
    }
    
    // 测试3：批量写入与单个写入的混合使用
    printf("\n--- Test 3: Mixed batch and single writes ---\n");
    
    // 单个写入
    test_data_t single_data = {3000, "SingleItem", 99.99f, true};
    if (fast_flash_write_table_data("BATCHWR", &single_data, sizeof(test_data_t)) != 0) {
        printf("Failed to perform single write\n");
        return -1;
    }
    
    // 再次批量写入
    test_data_t batch_data3[] = {
        {4000, "MixedBatch1", 111.11f, false},
        {4001, "MixedBatch2", 222.22f, true}
    };
    
    result = fast_flash_write_table_data_batch("BATCHWR", batch_data3, sizeof(test_data_t), 2);
    if (result != 0) {
        printf("Failed to perform mixed batch write\n");
        return -1;
    }
    
    // 检查最终数据数量
    count = fast_flash_get_table_count("BATCHWR");
    printf("Final data count after mixed writes: %u\n", count);
    if (count != 11) {
        printf("Expected 11, got %u\n", count);
        return -1;
    }
    
    // 验证混合写入的数据
    uint32_t expected_ids[] = {1000, 1001, 1002, 1003, 1004, 2000, 2001, 2002, 3000, 4000, 4001};
    for (int i = 0; i < 11; i++) {
        if (fast_flash_read_table_data("BATCHWR", i, &read_data, sizeof(test_data_t)) != 0) {
            printf("Failed to read mixed data at index %d\n", i);
            return -1;
        }
        
        if (read_data.id != expected_ids[i]) {
            printf("Mixed data mismatch at index %d, expected ID %u, got %u\n", 
                   i, expected_ids[i], read_data.id);
            return -1;
        }
    }
    
    // 测试4：批量写入超出容量限制
    printf("\n--- Test 4: Batch write exceeding capacity ---\n");
    
    // 创建一个容量较小的表
    if (fast_flash_create_table("SMALLTB", sizeof(test_data_t), 3) != 0) {
        printf("Failed to create SMALLTB table\n");
        return -1;
    }
    
    // 尝试批量写入超过容量限制的数据
    test_data_t overflow_data[] = {
        {5000, "Overflow1", 1.0f, true},
        {5001, "Overflow2", 2.0f, true},
        {5002, "Overflow3", 3.0f, true},
        {5003, "Overflow4", 4.0f, true}  // 超过容量（最大3个）
    };
    
    result = fast_flash_write_table_data_batch("SMALLTB", overflow_data, sizeof(test_data_t), 4);
    if (result != -2) {  // 期望返回超出容量错误
        printf("Expected capacity exceeded error (-2), got %d\n", result);
        return -1;
    }
    printf("Capacity exceeded test passed, correctly returned error %d\n", result);
    
    // 测试5：批量写入空数据
    printf("\n--- Test 5: Batch write with zero count ---\n");
    
    result = fast_flash_write_table_data_batch("BATCHWR", batch_data1, sizeof(test_data_t), 0);
    if (result != -1) {  // 期望返回参数错误
        printf("Expected parameter error (-1) for zero count, got %d\n", result);
        return -1;
    }
    printf("Zero count test passed, correctly returned error %d\n", result);
    
    // 测试6：性能对比测试
    printf("\n--- Test 6: Performance comparison ---\n");
    
    // 创建一个新表用于性能测试
    if (fast_flash_create_table("PERFTST", sizeof(test_data_t), 100) != 0) {
        printf("Failed to create PERFTST table\n");
        return -1;
    }
    
    // 重置性能统计
    win_flash_reset_perf_stats();
    
    // 批量写入10条数据
    test_data_t perf_batch_data[10];
    for (int i = 0; i < 10; i++) {
        perf_batch_data[i].id = 6000 + i;
        snprintf(perf_batch_data[i].name, sizeof(perf_batch_data[i].name), "PerfBatch%d", i);
        perf_batch_data[i].value = (float)(i * 10);
        perf_batch_data[i].active = (i % 2 == 0);
    }
    
    uint32_t start_time = get_time_ms();
    result = fast_flash_write_table_data_batch("PERFTST", perf_batch_data, sizeof(test_data_t), 10);
    uint32_t batch_time = get_time_ms() - start_time;
    
    if (result != 0) {
        printf("Failed to perform batch write for performance test\n");
        return -1;
    }
    
    printf("Batch write 10 items: %u ms\n", batch_time);
    
    // 验证批量写入的数据
    count = fast_flash_get_table_count("PERFTST");
    printf("Performance test data count: %u\n", count);
    if (count != 10) {
        printf("Expected 10, got %u\n", count);
        return -1;
    }
    
    // 验证表数据完整性
    if (fast_flash_validate_table_data("BATCHWR") != 0) {
        printf("BATCHWR table validation failed\n");
        return -1;
    }
    
    if (fast_flash_validate_table_data("SMALLTB") != 0) {
        printf("SMALLTB table validation failed\n");
        return -1;
    }
    
    if (fast_flash_validate_table_data("PERFTST") != 0) {
        printf("PERFTST table validation failed\n");
        return -1;
    }
    
    // 显示最终表信息
    flash_table_t table_info;
    if (fast_flash_get_table_info("BATCHWR", &table_info) == 0) {
        printf("BATCHWR table info: Name=%s, Size=%u, Used=%u\n",
               table_info.name, table_info.size, table_info.used_size);
    }
    
    printf("Batch write function test passed!\n");
    return 0;
}

int main(void) {
    // 设置日志级别为INFO，显示所有重要信息
    flash_log_set_level(LOG_LEVEL_DEBUG);
    
    printf("Fast Flash Database Test Suite\n");
    printf("==============================\n");
    
    // 先初始化Flash适配器（确保文件存在）
    if (win_flash_init() != 0) {
        printf("Failed to initialize flash adapter\n");
        return -1;
    }
    
    // 重置Flash（干净的测试环境）
    if (win_flash_reset() != 0) {
        printf("Failed to reset flash\n");
        return -1;
    }
    
    // 初始化系统
    if (fast_flash_init(&win_flash_ops, WIN_FLASH_TOTAL_SIZE, false) != 0) {
        printf("Failed to initialize fast flash\n");
        return -1;
    }
    
    int result = 0;
    
    // 运行所有测试
    result |= test_basic_operations();
    result |= test_multiple_tables();
    result |= test_table_deletion();
    result |= test_persistence();
    result |= test_new_table_management_functions();
    result |= test_clear_table_data_function();
    result |= test_batch_write_function();
    result |= test_garbage_collection();
    result |= test_space_management();

    
    // 最终状态
    printf("\n=== Final Status ===\n");
    fast_flash_dump_manager_table();
    printf("Final free space: %u bytes\n", fast_flash_get_free_size());
    
    // 打印性能统计
    win_flash_print_perf_stats();
    
    if (result == 0) {
        printf("\n All tests passed!\n");
    } else {
        printf("\n Some tests failed!\n");
    }
    
    return result;
}