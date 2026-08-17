#include "fast_flash_core.h"
#include "fast_flash_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 全局状态
static const flash_ops_t *g_flash_ops = NULL;
static uint32_t g_total_size = 0;
static bool g_allow_erase = false;

static flash_manager_table_t g_manager_table;
static bool g_manager_loaded = false;

// 当前写入位置管理
static uint32_t g_current_sector = 0;
static uint32_t g_current_offset = 0;

// 内部函数声明
static uint32_t calculate_crc32(const uint8_t *data, uint32_t length);
static uint32_t calculate_manager_table_crc(const flash_manager_table_t *table);
// static uint32_t align_to_sector_boundary(uint32_t addr);
static int load_manager_table(void);
static int save_manager_table(void);
static int find_free_table_slot(void);
static int find_table_index(const char *name);
static int allocate_table_space(uint32_t size, uint32_t *out_addr);
static int write_with_chunks(uint32_t addr, const uint8_t *data, uint32_t size);
static int validate_manager_table(const flash_manager_table_t *table);

// CRC32计算
static uint32_t calculate_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// 计算管理表CRC（从version字段开始计算）
static uint32_t calculate_manager_table_crc(const flash_manager_table_t *table) {
    uint8_t *crc_start = (uint8_t*)table + sizeof(uint16_t) + sizeof(uint32_t);
    uint32_t crc_length = sizeof(flash_manager_table_t) - sizeof(uint16_t) - sizeof(uint32_t);
    return calculate_crc32(crc_start, crc_length);
}

// 对齐到扇区边界
// static uint32_t align_to_sector_boundary(uint32_t addr) {
//     return (addr + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
// }

// 分块写入（确保可打断性）
static int write_with_chunks(uint32_t addr, const uint8_t *data, uint32_t size) {
    const uint8_t *src = data;
    uint32_t remain = size;
    uint32_t current_addr = addr;

    while (remain > 0) {
        uint32_t chunk_size = (remain > FLASH_WRITE_CHUNK_SIZE) ? FLASH_WRITE_CHUNK_SIZE : remain;

        int result = g_flash_ops->write(current_addr, src, chunk_size);
        if (result != 0) {
            TRACE_DEBUG("Write failed at addr=0x%08X, size=%u\n", current_addr, chunk_size);
            return result;
        }

        src += chunk_size;
        current_addr += chunk_size;
        remain -= chunk_size;
    }

    return 0;
}

// 验证管理表有效性
static int validate_manager_table(const flash_manager_table_t *table) {
    if (!table) return -1;

    if (table->magic != MAGIC_NUMBER_MANAGER) {
        TRACE_ERROR("Invalid manager table magic: 0x%04X\n", table->magic);
        return -1;
    }

    if (table->version != MANAGER_TABLE_VERSION) {
        TRACE_ERROR("Unsupported manager table version: %u\n", table->version);
        return -1;
    }

    // CRC校验（从version字段开始计算，跳过magic和crc字段）
    uint32_t calculated_crc = calculate_manager_table_crc(table);
    if (calculated_crc != table->crc) {
        TRACE_ERROR("Manager table CRC mismatch: calculated=0x%08X, stored=0x%08X\n",
                   calculated_crc, table->crc);
        return -1;
    }

    return 0;
}

// 加载管理表（紧密排布的链表结构）
static int load_manager_table(void) {
    uint32_t addr = 0;
    flash_manager_table_t candidate;
    flash_manager_table_t last_valid_table;
    uint32_t last_valid_addr = 0;
    bool found_valid = false;

    TRACE_DEBUG("Loading manager table...\n");

    // 重置全局状态
    g_manager_loaded = false;
    memset(&g_manager_table, 0, sizeof(g_manager_table));
    g_current_sector = 0;
    g_current_offset = 0;

    // 遍历管理表链表，紧密排布不需要对齐到扇区边界
    while (addr < g_total_size) {
        int result = g_flash_ops->read(addr, (uint8_t*)&candidate, sizeof(candidate));
        if (result != 0) {
            TRACE_DEBUG("Failed to read manager table at addr=0x%08X\n", addr);
            break;
        }

        // 检查魔数
        if (candidate.magic != MAGIC_NUMBER_MANAGER) {
            TRACE_DEBUG("Invalid magic at addr=0x%08X, stopping search\n", addr);
            break;
        }

        // 验证表有效性
        if (validate_manager_table(&candidate) != 0) {
            TRACE_DEBUG("Invalid manager table at addr=0x%08X, stopping search\n", addr);
            break;
        }

        // 保存当前有效表
        memcpy(&last_valid_table, &candidate, sizeof(candidate));
        last_valid_addr = addr;
        found_valid = true;

        // 检查下一个管理表地址是否有效
        if (candidate.next_manager_addr == 0 ||
            candidate.next_manager_addr >= g_total_size ||
            candidate.next_manager_addr <= addr) {
            // 这是最新有效的管理表
            memcpy(&g_manager_table, &candidate, sizeof(candidate));
            g_manager_loaded = true;
            TRACE_INFO("g_manager_loaded %d", g_manager_loaded);

            // 计算数据区域结束位置，这就是下一个写入位置
            uint32_t data_end = addr + sizeof(flash_manager_table_t);
            for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
                if (candidate.tables[i].status == TABLE_STATUS_VALID) {
                    uint32_t table_end = candidate.tables[i].addr + candidate.tables[i].size;
                    if (table_end > data_end) {
                        data_end = table_end;
                    }
                }
            }

            // 跳过预留的管理表空间（数据区强制从扇区1起，避开扇区0管理表）
            g_current_sector = data_end / FLASH_SECTOR_SIZE;
            if (g_current_sector == 0) g_current_sector = 1;
            g_current_offset = data_end % FLASH_SECTOR_SIZE;

            TRACE_INFO("Loaded manager table at 0x%08X, data end at 0x%08X, next reserved at 0x%08X\n",
                      addr, data_end, candidate.next_manager_addr);
            return 0;
        }

        // 检查下一个管理表是否存在且有效
        uint32_t next_addr = candidate.next_manager_addr;
        flash_manager_table_t next_candidate;
        
        result = g_flash_ops->read(next_addr, (uint8_t*)&next_candidate, sizeof(next_candidate));
        if (result != 0) {
            // 无法读取下一个表，说明当前表是最后一个有效表
            TRACE_DEBUG("Failed to read next manager table at 0x%08X, using current table\n", next_addr);
            memcpy(&g_manager_table, &candidate, sizeof(candidate));
            g_manager_loaded = true;

            // 计算数据区域结束位置（基准为当前管理表末尾，而非下一个管理表地址）
            uint32_t data_end = addr + sizeof(flash_manager_table_t);
            for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
                if (candidate.tables[i].status == TABLE_STATUS_VALID) {
                    uint32_t table_end = candidate.tables[i].addr + candidate.tables[i].size;
                    if (table_end > data_end) {
                        data_end = table_end;
                    }
                }
            }

            // 数据区强制从扇区1起，避开扇区0管理表
            g_current_sector = data_end / FLASH_SECTOR_SIZE;
            if (g_current_sector == 0) g_current_sector = 1;
            g_current_offset = data_end % FLASH_SECTOR_SIZE;

            TRACE_INFO("Using last valid manager table at 0x%08X (next table unreadable)\n", addr);
            return 0;
        }

        // 检查下一个表的魔数和有效性
        if (next_candidate.magic != MAGIC_NUMBER_MANAGER || 
            validate_manager_table(&next_candidate) != 0) {
            // 下一个表无效，说明当前表是最后一个有效表
            TRACE_DEBUG("Next manager table at 0x%08X is invalid, using current table\n", next_addr);
            memcpy(&g_manager_table, &candidate, sizeof(candidate));
            g_manager_loaded = true;

            // 计算数据区域结束位置（基准为当前管理表末尾，而非下一个管理表地址）
            uint32_t data_end = addr + sizeof(flash_manager_table_t);
            for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
                if (candidate.tables[i].status == TABLE_STATUS_VALID) {
                    uint32_t table_end = candidate.tables[i].addr + candidate.tables[i].size;
                    if (table_end > data_end) {
                        data_end = table_end;
                    }
                }
            }

            // 数据区强制从扇区1起，避开扇区0管理表
            g_current_sector = data_end / FLASH_SECTOR_SIZE;
            if (g_current_sector == 0) g_current_sector = 1;
            g_current_offset = data_end % FLASH_SECTOR_SIZE;

            TRACE_INFO("Using last valid manager table at 0x%08X (next table invalid)\n", addr);
            return 0;
        }

        addr = next_addr;
        TRACE_DEBUG("Found manager table at 0x%08X, searching next at 0x%08X...\n", last_valid_addr, candidate.next_manager_addr);
    }

    if (found_valid) {
        // 使用最后一个找到的有效表
        memcpy(&g_manager_table, &last_valid_table, sizeof(last_valid_table));
        g_manager_loaded = true;
        
        // 计算数据区域结束位置
        uint32_t data_end = last_valid_addr + sizeof(flash_manager_table_t);
        for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
            if (last_valid_table.tables[i].status == TABLE_STATUS_VALID) {
                uint32_t table_end = last_valid_table.tables[i].addr + last_valid_table.tables[i].size;
                if (table_end > data_end) {
                    data_end = table_end;
                }
            }
        }
        
        g_current_sector = data_end / FLASH_SECTOR_SIZE;
        if (g_current_sector == 0) g_current_sector = 1;  /* 数据区避开扇区0(管理表) */
        g_current_offset = data_end % FLASH_SECTOR_SIZE;
        
        TRACE_INFO("Using last found manager table at 0x%08X\n", last_valid_addr);
        return 0;
    }

    // 没有找到任何有效管理表，初始化新的
    TRACE_INFO("No valid manager table found, initializing new one\n");

    memset(&g_manager_table, 0, sizeof(g_manager_table));
    g_manager_table.magic = MAGIC_NUMBER_MANAGER;
    g_manager_table.version = MANAGER_TABLE_VERSION;
    g_manager_table.total_size = g_total_size;
    g_manager_table.used_size = 0;
    g_manager_table.table_count = 0;

    // 紧密排布：下一个管理表位置紧跟着当前管理表
    uint32_t next_mgr = sizeof(flash_manager_table_t);
    g_manager_table.next_manager_addr = next_mgr;

    // 初始化时需要擦除第一个扇区，临时允许擦除
    bool original_allow_erase = g_allow_erase;
    g_allow_erase = true;
    if (g_flash_ops->erase(0, FLASH_SECTOR_SIZE) != 0) {
        TRACE_ERROR("Failed to erase first sector for manager table\n");
        g_allow_erase = original_allow_erase;
        return -1;
    }
    g_allow_erase = original_allow_erase;

    // 写入管理表
    g_manager_table.crc = calculate_manager_table_crc(&g_manager_table);
    if (write_with_chunks(0, (uint8_t*)&g_manager_table, sizeof(g_manager_table)) != 0) {
        TRACE_ERROR("Failed to write initial manager table\n");
        return -1;
    }

    // 设置写入位置：管理表固定在扇区0，表数据从扇区1开始，避免互相擦除破坏
    g_current_sector = 1;
    g_current_offset = 0;

    g_manager_loaded = true;
    TRACE_INFO("g_manager_loaded %d", g_manager_loaded);
    TRACE_INFO("Initialized new manager table at 0x%08X, g_current_offset at 0x%08X, next reserved at 0x%08X\n", 0, g_current_offset, next_mgr);

    return 0;
}

// 保存管理表（紧密排布）
static int save_manager_table(void) {
    if (!g_manager_loaded) {
        TRACE_ERROR("Manager table not loaded\n");
        return -1;
    }

    /*
     * 管理表固定存储在首节点(addr=0)，每次保存均原地刷新该节点。
     * 这样"掉电重放"时 load_manager_table 从 addr=0 起必能读到最新管理表，
     * 避免旧实现中"追加写新副本"导致首节点被后续扇区擦除破坏、整条链表丢失的问题。
     * 表数据从扇区 1 开始，与管理表所在扇区 0 隔离，擦除扇区 0 不会破坏数据。
     */
    uint32_t new_addr = 0;

    // 计算下一个管理表的预留位置（在当前写入位置之后）
    uint32_t current_write_pos = g_current_sector * FLASH_SECTOR_SIZE + g_current_offset;
    if (current_write_pos < FLASH_SECTOR_SIZE) {
        current_write_pos = FLASH_SECTOR_SIZE;  // 数据区强制从扇区1起，避开扇区0管理表
    }
    uint32_t next_reserved = current_write_pos;

    // 检查是否需要跳到下一个扇区（为下一个管理表预留空间）
    uint32_t current_sector = current_write_pos / FLASH_SECTOR_SIZE;
    uint32_t current_offset = current_write_pos % FLASH_SECTOR_SIZE;
    uint32_t available_in_sector = FLASH_SECTOR_SIZE - current_offset;

    // 如果当前扇区剩余空间不足以容纳管理表，跳到下一个扇区
    if (sizeof(flash_manager_table_t) > available_in_sector) {
        current_sector++;
        next_reserved = current_sector * FLASH_SECTOR_SIZE;
    }

    // 确保有足够空间
    if (next_reserved + sizeof(flash_manager_table_t) > g_total_size) {
        TRACE_ERROR("Insufficient space for next manager table\n");
        return -1;
    }

    // 检查是否需要擦除目标区域
    bool need_erase = false;
    if (g_allow_erase) {
        // 检查目标地址是否已经被使用过（非0xFF状态）
        uint8_t test_byte;
        if (g_flash_ops->read(new_addr, &test_byte, 1) == 0 && test_byte != 0xFF) {
            need_erase = true;
        }
    }

    // 如果需要擦除且允许擦除，则擦除目标扇区
    if (need_erase && g_allow_erase) {
        uint32_t start_sector = new_addr / FLASH_SECTOR_SIZE;
        uint32_t end_addr = new_addr + sizeof(flash_manager_table_t);
        uint32_t end_sector = (end_addr - 1) / FLASH_SECTOR_SIZE;  // 修正边界计算

        for (uint32_t sector = start_sector; sector <= end_sector; sector++) {
            if (g_flash_ops->erase(sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE) != 0) {
                TRACE_ERROR("Failed to erase sector %u for manager table\n", sector);
                return -2;  // 表示需要擦除但不允许
            }
        }
        TRACE_DEBUG("Erased sectors %u-%u for new manager table at 0x%08X\n", 
                   start_sector, end_sector, new_addr);
    }

    // 先更新管理表信息（首节点即最新管理表，next_manager_addr 固定为 0）
    g_manager_table.next_manager_addr = 0;
    g_manager_table.crc = calculate_manager_table_crc(&g_manager_table);

    // 写入新管理表
    TRACE_DEBUG("Writing new manager table to 0x%08X, size=%u\n", new_addr, sizeof(g_manager_table));
    if (write_with_chunks(new_addr, (uint8_t*)&g_manager_table, sizeof(g_manager_table)) != 0) {
        TRACE_ERROR("Failed to write new manager table to 0x%08X\n", new_addr);
        return -1;
    }

    // 更新写入位置（在下一个预留管理表之后）
    uint32_t new_pos = next_reserved + sizeof(flash_manager_table_t);
    g_current_sector = new_pos / FLASH_SECTOR_SIZE;
    g_current_offset = new_pos % FLASH_SECTOR_SIZE;
    if (g_current_sector == 0) {
        g_current_sector = 1;  /* 数据区强制避开扇区0(管理表所在) */
        g_current_offset = 0;
    }

    TRACE_INFO("Saved manager table to 0x%08X, g_current_offset at 0x%08X, next reserved at 0x%08X\n",
              new_addr, g_current_offset + g_current_sector * FLASH_SECTOR_SIZE, next_reserved);

    return 0;
}

// 查找空闲表槽
static int find_free_table_slot(void) {
    for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
        if (g_manager_table.tables[i].status == TABLE_STATUS_INVALID) {
            return i;
        }
    }
    return -1;
}

// 查找表索引
static int find_table_index(const char *name) {
    if (!name) return -1;

    for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
        if (g_manager_table.tables[i].status == TABLE_STATUS_VALID &&
            strncmp(g_manager_table.tables[i].name, name, TABLE_NAME_MAX_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

/// 分配表空间（确保不跨扇区，其他时候紧密排布）
static int allocate_table_space(uint32_t size, uint32_t *out_addr) {
    if (!out_addr || size == 0) {
        return -1;
    }

    if (size > FLASH_SECTOR_SIZE) {
        TRACE_ERROR("Table size %u exceeds sector size %u\n", size, FLASH_SECTOR_SIZE);
        return -1;
    }

    // 强制数据区从扇区1起，避开扇区0（管理表固定驻留扇区0，会被反复擦除）
    if (g_current_sector == 0) {
        g_current_sector = 1;
        g_current_offset = 0;
    }

    // 当前空闲地址 = g_current_sector * FLASH_SECTOR_SIZE + g_current_offset
    uint32_t free_addr = g_current_sector * FLASH_SECTOR_SIZE + g_current_offset;
    uint32_t sector_start = (free_addr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    uint32_t offset_in_sector = free_addr % FLASH_SECTOR_SIZE;

    // 检查当前扇区剩余空间是否足够
    if (offset_in_sector + size > FLASH_SECTOR_SIZE) {
        // 跨扇区了，跳到下一个扇区开头
        sector_start += FLASH_SECTOR_SIZE;
        offset_in_sector = 0;

        // 检查总空间
        if (sector_start + size > g_total_size) {
            TRACE_ERROR("Insufficient flash space for table of size %u\n", size);
            return -2;
        }

        // 擦除新扇区（如果允许）
        if (g_allow_erase) {
            if (g_flash_ops->erase(sector_start, FLASH_SECTOR_SIZE) != 0) {
                TRACE_ERROR("Failed to erase sector at 0x%08X\n", sector_start);
                return -2;
            }
        }
    }

    *out_addr = sector_start + offset_in_sector;

    // 更新全局空闲位置（指向新表之后）
    g_current_sector = (*out_addr + size) / FLASH_SECTOR_SIZE;
    g_current_offset = (*out_addr + size) % FLASH_SECTOR_SIZE;

    TRACE_DEBUG("Allocated table space: addr=0x%08X, size=%u, next free=0x%08X\n",
                *out_addr, size,
                g_current_sector * FLASH_SECTOR_SIZE + g_current_offset);

    return 0;
}

// === 公共API实现 ===

int fast_flash_init(const flash_ops_t *ops, uint32_t total_size, bool allow_erase) {
#ifdef RS_FLASH_DEBUG_OFF
#else
    flash_log_set_level(LOG_LEVEL_DEBUG);
#endif
    if (!ops || !ops->init || !ops->read || !ops->write || !ops->erase) {
        TRACE_ERROR("Invalid flash operations\n");
        return -1;
    }

    g_flash_ops = ops;
    g_total_size = total_size;
    g_allow_erase = allow_erase;

    // 初始化Flash设备
    if (g_flash_ops->init() != 0) {
        TRACE_ERROR("Flash device initialization failed\n");
        return -1;
    }

    // 加载管理表
    if (load_manager_table() != 0) {
        TRACE_ERROR("Failed to load manager table\n");
        return -1;
    }

    TRACE_INFO("Fast Flash Core initialized successfully\n");
    return 0;
}

int fast_flash_create_table(const char *name, uint32_t struct_size, uint32_t max_structs) {
    if (!name || !g_manager_loaded) {
        return -1;
    }

    // 检查表是否已存在
    if (find_table_index(name) >= 0) {
        TRACE_WARN("Table '%s' already exists\n", name);
        return -1;
    }

    // 查找空闲槽
    int slot = find_free_table_slot();
    if (slot < 0) {
        TRACE_ERROR("No free table slots available\n");
        return -1;
    }

    // 计算表大小（只需要表头大小，不预分配数据空间）
    uint32_t table_size = sizeof(table_header_t);

    // 分配空间
    uint32_t table_addr;
    int result = allocate_table_space(table_size, &table_addr);
    if (result != 0) {
        TRACE_DEBUG("Failed to allocate space for table '%s'\n", name);
        return result;  // -2表示空间不足
    }

    // 初始化表头
    table_header_t header;
    header.magic = MAGIC_NUMBER_TABLE;
    strncpy(header.name, name, TABLE_NAME_MAX_LEN - 1);
    header.name[TABLE_NAME_MAX_LEN - 1] = '\0';
    header.table_size = sizeof(table_header_t) + struct_size * max_structs;  // 记录理论最大大小
    header.data_len = 0;
    header.struct_size = struct_size;
    header.struct_nums = 0;
    header.data_crc = 0;

    // 写入表头
    if (write_with_chunks(table_addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to write table header for '%s'\n", name);
        return -1;
    }

    // 更新管理表信息
    flash_table_info_t *table_info = &g_manager_table.tables[slot];
    strncpy(table_info->name, name, TABLE_NAME_MAX_LEN - 1);
    table_info->name[TABLE_NAME_MAX_LEN - 1] = '\0';
    table_info->addr = table_addr;
    table_info->size = sizeof(table_header_t);  // 实际分配的空间只包含表头
    table_info->used_size = sizeof(header);
    table_info->magic = MAGIC_NUMBER_TABLE;
    table_info->status = TABLE_STATUS_VALID;
    table_info->reserved = 0;
    table_info->next_manager_addr = 0;

    g_manager_table.table_count++;
    g_manager_table.used_size += sizeof(table_header_t);  // 只增加表头大小

    // 保存管理表
    result = save_manager_table();
    if (result != 0) {
        TRACE_DEBUG("Failed to save manager table after creating '%s'\n", name);
        return result;
    }

    TRACE_DEBUG("Created table '%s' at addr=0x%08X, size=%u\n", name, table_addr, table_size);
    return 0;
}

int fast_flash_delete_table(const char *name) {
    if (!name || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", name);
        return -1;
    }

    // 标记为删除
    g_manager_table.tables[idx].status = TABLE_STATUS_DELETED;
    g_manager_table.table_count--;

    int result = save_manager_table();
    if (result != 0) {
        TRACE_DEBUG("Failed to save manager table after deleting '%s'\n", name);
        return result;
    }

    TRACE_DEBUG("Deleted table '%s'\n", name);
    return 0;
}

int fast_flash_write_table_data(const char *table_name, const void *data, uint32_t size) {
    if (!table_name || !data || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取当前表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return -1;
    }

    // 检查传入的数据大小是否与结构体大小匹配
    if (size != header.struct_size) {
        TRACE_DEBUG("Data size %u doesn't match table struct size %u for '%s'\n",
                   size, header.struct_size, table_name);
        return -1;
    }

    // 计算新的数据长度（在现有数据基础上添加新数据）
    uint32_t new_data_len = header.data_len + size;

    // 分配新的表空间（表头 + 新的数据）
    uint32_t new_table_addr;
    int result = allocate_table_space(sizeof(table_header_t) + new_data_len, &new_table_addr);
    if (result != 0) {
        TRACE_DEBUG("Failed to allocate space for expanded table '%s'\n", table_name);
        return result;
    }

    // 分配临时缓冲区用于存储所有数据（旧数据 + 新数据）
    uint8_t *all_data = malloc(new_data_len);
    if (!all_data) {
        TRACE_DEBUG("Memory allocation failed for table '%s'\n", table_name);
        return -1;
    }

    // 读取旧数据（如果有的话）
    if (header.data_len > 0) {
        if (g_flash_ops->read(table_info->addr + sizeof(header), all_data, header.data_len) != 0) {
            TRACE_DEBUG("Failed to read old data for table '%s'\n", table_name);
            free(all_data);
            return -1;
        }
    }

    // 添加新数据到缓冲区末尾
    memcpy(all_data + header.data_len, data, size);

    // 更新表头
    header.data_len = new_data_len;
    header.struct_nums = new_data_len / header.struct_size;
    header.data_crc = calculate_crc32(all_data, new_data_len);

    // 写入新表头
    if (write_with_chunks(new_table_addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to write new table header for '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    // 写入所有数据
    if (write_with_chunks(new_table_addr + sizeof(header), all_data, new_data_len) != 0) {
        TRACE_DEBUG("Failed to write table data for '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    free(all_data);

    // 更新管理表信息
    table_info->addr = new_table_addr;
    table_info->size = header.table_size;
    table_info->used_size = sizeof(table_header_t) + new_data_len;

    // 保存管理表
    result = save_manager_table();
    if (result != 0) {
        TRACE_DEBUG("Failed to save manager table after writing '%s'\n", table_name);
        return result;
    }

    TRACE_DEBUG("Added data to table '%s', new total size: %u bytes\n", table_name, new_data_len);
    return 0;
}

int fast_flash_read_table_data(const char *table_name, uint32_t index, void *buffer, uint32_t size) {
    if (!table_name || !buffer || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return -1;
    }

    // 检查传入的数据大小是否与结构体大小匹配
    if (size != header.struct_size) {
        TRACE_DEBUG("Buffer size %u doesn't match table struct size %u for '%s'\n",
                   size, header.struct_size, table_name);
        return -1;
    }

    // 检查序号是否有效
    if (index >= header.struct_nums) {
        TRACE_DEBUG("Index %u exceeds table data count %u for '%s'\n",
                   index, header.struct_nums, table_name);
        return -1;
    }

    // 计算数据偏移量
    uint32_t offset = index * header.struct_size;
    uint32_t data_addr = table_info->addr + sizeof(table_header_t) + offset;

    return g_flash_ops->read(data_addr, (uint8_t*)buffer, size);
}

int fast_flash_get_table_info(const char *table_name, flash_table_t *info) {
    if (!table_name || !info || !g_manager_loaded) {
        TRACE_INFO("g_manager_loaded %d", g_manager_loaded);
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];
    strncpy(info->name, table_info->name, TABLE_NAME_MAX_LEN-1);
    info->addr = table_info->addr;
    info->size = table_info->size;
    info->used_size = table_info->used_size;
    info->magic = table_info->magic;
    info->status = table_info->status;

    return 0;
}

int fast_flash_list_tables(flash_table_t *tables, int max_count) {
    if (!tables || !g_manager_loaded || max_count <= 0) {
        return -1;
    }

    int count = 0;
    for (int i = 0; i < MAX_TABLES_ALL_SECTOR && count < max_count; i++) {
        if (g_manager_table.tables[i].status == TABLE_STATUS_VALID) {
            flash_table_info_t *src = &g_manager_table.tables[i];
            strncpy(tables[count].name, src->name, TABLE_NAME_MAX_LEN);
            tables[count].addr = src->addr;
            tables[count].size = src->size;
            tables[count].used_size = src->used_size;
            tables[count].magic = src->magic;
            tables[count].status = src->status;
            count++;
        }
    }

    return count;
}

bool fast_flash_table_exists(const char *name) {
    if (!name || !g_manager_loaded) {
        TRACE_INFO("g_manager_loaded %d", g_manager_loaded);
        return false;
    }

    return find_table_index(name) >= 0;
}

void fast_flash_set_erase_allowed(bool allowed) {
    g_allow_erase = allowed;
    TRACE_DEBUG("Erase operations %s\n", allowed ? "allowed" : "disallowed");
}

bool fast_flash_is_erase_allowed(void) {
    return g_allow_erase;
}

int fast_flash_gc(void) {
    if (!g_manager_loaded) {
        TRACE_DEBUG("Manager table not loaded\n");
        return -1;
    }

    if (!g_allow_erase) {
        TRACE_DEBUG("Erase not allowed, cannot perform garbage collection\n");
        return -2;
    }

    TRACE_DEBUG("Starting garbage collection...\n");

    uint32_t total_sectors = g_total_size / FLASH_SECTOR_SIZE;
    uint32_t empty_sector = 0xFFFFFFFF;  // 标记为未找到

    // === 阶段1：准备阶段 - 寻找空扇区并准备缓存 ===

    // 1. 检查每个扇区是否有有效表
    for (uint32_t sector = 0; sector < total_sectors; sector++) {
        bool has_valid_table = false;

        // 检查当前扇区是否有有效表
        for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
            if (g_manager_table.tables[i].status == TABLE_STATUS_VALID) {
                uint32_t table_sector = g_manager_table.tables[i].addr / FLASH_SECTOR_SIZE;
                if (table_sector == sector) {
                    has_valid_table = true;
                    break;
                }
            }
        }

        if (!has_valid_table) {
            empty_sector = sector;
            TRACE_DEBUG("Found empty sector: %u\n", sector);
            break;
        }
    }

    // 2. 收集有效表并按地址排序
    flash_table_info_t valid_tables[MAX_TABLES_ALL_SECTOR];
    int valid_count = 0;

    for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
        if (g_manager_table.tables[i].status == TABLE_STATUS_VALID) {
            valid_tables[valid_count++] = g_manager_table.tables[i];
        }
    }

    // 按地址排序（确保按照位置顺序写入）
    for (int i = 0; i < valid_count - 1; i++) {
        for (int j = i + 1; j < valid_count; j++) {
            if (valid_tables[i].addr > valid_tables[j].addr) {
                flash_table_info_t temp = valid_tables[i];
                valid_tables[i] = valid_tables[j];
                valid_tables[j] = temp;
            }
        }
    }

    if (empty_sector != 0xFFFFFFFF) {
        // === 阶段2：有空扇区时的处理 ===

        // 2.1 擦除空扇区作为缓存扇区
        if (g_flash_ops->erase(empty_sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE) != 0) {
            TRACE_DEBUG("Failed to erase cache sector %u\n", empty_sector);
            return -1;
        }

        TRACE_DEBUG("Using sector %u as cache sector\n", empty_sector);

        // 2.2 将第一个扇区的有效数据搬运到缓存扇区
        uint32_t cache_write_pos = empty_sector * FLASH_SECTOR_SIZE;

        // 收集第一个扇区的有效表
        flash_table_info_t first_sector_tables[MAX_TABLES_ALL_SECTOR];
        int first_sector_count = 0;

        for (int i = 0; i < valid_count; i++) {
            uint32_t table_sector = valid_tables[i].addr / FLASH_SECTOR_SIZE;
            if (table_sector == 0) {
                first_sector_tables[first_sector_count++] = valid_tables[i];
            }
        }

        // 搬运第一个扇区的数据到缓存扇区
        for (int i = 0; i < first_sector_count; i++) {
            uint32_t table_size = first_sector_tables[i].size;

            // 读取表数据
            uint8_t *temp_data = malloc(table_size);
            if (!temp_data) {
                TRACE_DEBUG("Memory allocation failed during cache preparation\n");
                return -1;
            }

            if (g_flash_ops->read(first_sector_tables[i].addr, temp_data, table_size) != 0) {
                TRACE_DEBUG("Failed to read table '%s' during cache preparation\n", first_sector_tables[i].name);
                free(temp_data);
                return -1;
            }

            // 写入缓存扇区
            if (write_with_chunks(cache_write_pos, temp_data, table_size) != 0) {
                TRACE_DEBUG("Failed to write table '%s' to cache sector\n", first_sector_tables[i].name);
                free(temp_data);
                return -1;
            }

            // 更新RAM中的管理表
            for (int j = 0; j < MAX_TABLES_ALL_SECTOR; j++) {
                if (g_manager_table.tables[j].status == TABLE_STATUS_VALID &&
                    strncmp(g_manager_table.tables[j].name, first_sector_tables[i].name, TABLE_NAME_MAX_LEN) == 0) {
                    g_manager_table.tables[j].addr = cache_write_pos;
                    break;
                }
            }

            cache_write_pos += table_size;
            free(temp_data);
        }

        // 2.3 擦除第一扇区（现在变成空扇区）
        if (g_flash_ops->erase(0, FLASH_SECTOR_SIZE) != 0) {
            TRACE_DEBUG("Failed to erase first sector after cache preparation\n");
            return -1;
        }

        // 2.4 现在第一扇区是空的，缓存扇区有数据，开始正式垃圾回收

    } else {
        // === 阶段3：没有空扇区时的处理 ===
        TRACE_DEBUG("No empty sector found, erasing first sector and abandoning data\n");

        // 擦除第一个扇区，放弃数据
        if (g_flash_ops->erase(0, FLASH_SECTOR_SIZE) != 0) {
            TRACE_DEBUG("Failed to erase first sector\n");
            return -1;
        }

        // 重置管理表
        memset(&g_manager_table, 0, sizeof(g_manager_table));
        g_manager_table.magic = MAGIC_NUMBER_MANAGER;
        g_manager_table.version = MANAGER_TABLE_VERSION;
        g_manager_table.total_size = g_total_size;
        g_manager_table.used_size = 0;
        g_manager_table.table_count = 0;

        // 擦除其他所有扇区
        for (uint32_t sector = 1; sector < total_sectors; sector++) {
            g_flash_ops->erase(sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        }

        // 写入空管理表到第一扇区开头
        g_manager_table.next_manager_addr = sizeof(flash_manager_table_t);
        g_manager_table.crc = calculate_manager_table_crc(&g_manager_table);

        if (write_with_chunks(0, (uint8_t*)&g_manager_table, sizeof(g_manager_table)) != 0) {
            TRACE_DEBUG("Failed to write empty manager table\n");
            return -1;
        }

        // 更新全局状态
        g_current_sector = 0;
        g_current_offset = sizeof(flash_manager_table_t) * 2;  // 当前管理表 + 下一个预留空间

        TRACE_DEBUG("GC completed: first sector erased, all data abandoned\n");
        return 0;
    }

    // === 阶段4：正式垃圾回收（有空扇区的情况）===

    TRACE_DEBUG("Starting formal garbage collection with empty sector 0\n");

    // 4.1 重新收集所有有效表（因为地址可能已经更新）
    valid_count = 0;
    for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
        if (g_manager_table.tables[i].status == TABLE_STATUS_VALID) {
            valid_tables[valid_count++] = g_manager_table.tables[i];
        }
    }

    // 重新按地址排序
    for (int i = 0; i < valid_count - 1; i++) {
        for (int j = i + 1; j < valid_count; j++) {
            if (valid_tables[i].addr > valid_tables[j].addr) {
                flash_table_info_t temp = valid_tables[i];
                valid_tables[i] = valid_tables[j];
                valid_tables[j] = temp;
            }
        }
    }

    // 4.2 在第一扇区预留管理表空间，开始写入有效表
    uint32_t current_write_pos = 0 + sizeof(flash_manager_table_t);  // 第一扇区预留管理表空间
    uint32_t current_sector = 0;

    for (int i = 0; i < valid_count; i++) {
        uint32_t table_size = valid_tables[i].size;

        // 检查当前扇区剩余空间
        uint32_t sector_end = (current_sector + 1) * FLASH_SECTOR_SIZE;
        if (current_write_pos + table_size > sector_end) {
            // 需要切换到下一个扇区
            current_sector++;
            current_write_pos = current_sector * FLASH_SECTOR_SIZE;

            // 擦除目标扇区
            if (g_flash_ops->erase(current_write_pos, FLASH_SECTOR_SIZE) != 0) {
                TRACE_DEBUG("Failed to erase sector %u during GC\n", current_sector);
                return -1;
            }
        }

        // 读取表数据
        uint8_t *temp_data = malloc(table_size);
        if (!temp_data) {
            TRACE_DEBUG("Memory allocation failed during formal GC\n");
            return -1;
        }

        if (g_flash_ops->read(valid_tables[i].addr, temp_data, table_size) != 0) {
            TRACE_DEBUG("Failed to read table '%s' during formal GC\n", valid_tables[i].name);
            free(temp_data);
            return -1;
        }

        // 写入新位置
        if (write_with_chunks(current_write_pos, temp_data, table_size) != 0) {
            TRACE_DEBUG("Failed to write table '%s' during formal GC\n", valid_tables[i].name);
            free(temp_data);
            return -1;
        }

        // 更新RAM中的管理表
        for (int j = 0; j < MAX_TABLES_ALL_SECTOR; j++) {
            if (g_manager_table.tables[j].status == TABLE_STATUS_VALID &&
                strncmp(g_manager_table.tables[j].name, valid_tables[i].name, TABLE_NAME_MAX_LEN) == 0) {
                g_manager_table.tables[j].addr = current_write_pos;
                break;
            }
        }

        current_write_pos += table_size;
        free(temp_data);
    }

    // 4.3 计算下一个管理表预留位置
    uint32_t next_manager_pos = current_write_pos;
    g_manager_table.next_manager_addr = next_manager_pos;
    g_manager_table.used_size = next_manager_pos;  // 更新已使用大小

    // 4.4 写入管理表到第一扇区开头
    g_manager_table.crc = calculate_manager_table_crc(&g_manager_table);
    if (write_with_chunks(0, (uint8_t*)&g_manager_table, sizeof(g_manager_table)) != 0) {
        TRACE_DEBUG("Failed to write manager table during formal GC\n");
        return -1;
    }

    // 4.5 擦除后续所有扇区
    for (uint32_t sector = current_sector + 1; sector < total_sectors; sector++) {
        g_flash_ops->erase(sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    }

    // 4.6 更新全局状态
    g_current_sector = current_sector;
    g_current_offset = current_write_pos % FLASH_SECTOR_SIZE;

    TRACE_DEBUG("GC completed: valid tables compacted to sectors 0-%u\n", current_sector);
    return 0;
}

void fast_flash_dump_manager_table(void) {
    if (!g_manager_loaded) {
        TRACE_DEBUG("Manager table not loaded\n");
        return;
    }

    TRACE_DEBUG("=== Manager Table Info ===\n");
    TRACE_DEBUG("Magic: 0x%04X\n", g_manager_table.magic);
    TRACE_DEBUG("Version: %u\n", g_manager_table.version);
    TRACE_DEBUG("Table Count: %u\n", g_manager_table.table_count);
    TRACE_DEBUG("Total Size: %u\n", g_manager_table.total_size);
    TRACE_DEBUG("Used Size: %u\n", g_manager_table.used_size);
    TRACE_DEBUG("Next Manager Addr: 0x%08X\n", g_manager_table.next_manager_addr);
    TRACE_DEBUG("CRC: 0x%08X\n", g_manager_table.crc);

    TRACE_DEBUG("\n=== Tables ===\n");
    for (int i = 0; i < MAX_TABLES_ALL_SECTOR; i++) {
        flash_table_info_t *table = &g_manager_table.tables[i];
        if (table->status == TABLE_STATUS_VALID) {
            TRACE_DEBUG("[%u] Name: %-8s Addr: 0x%08X Size: %5u Used: %5u Magic: 0x%04X\n",
                   i, table->name, table->addr, table->size, table->used_size, table->magic);
        }
    }
}

uint32_t fast_flash_get_total_size(void) {
    return g_total_size;
}

uint32_t fast_flash_get_used_size(void) {
    return g_manager_loaded ? g_manager_table.used_size : 0;
}

uint32_t fast_flash_get_free_size(void) {
    return g_total_size - fast_flash_get_used_size();
}

int fast_flash_validate_table_data(const char *table_name) {
    if (!table_name || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];
    table_header_t header;

    // 读取表头
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        return -1;
    }

    // 检查魔数
    if (header.magic != MAGIC_NUMBER_TABLE) {
        TRACE_DEBUG("Invalid table magic for '%s'\n", table_name);
        return -1;
    }

    // 验证数据CRC
    if (header.data_len > 0) {
        uint8_t *data = malloc(header.data_len);
        if (!data) {
            return -1;
        }

        int result = g_flash_ops->read(table_info->addr + sizeof(header), data, header.data_len);
        if (result != 0) {
            TRACE_DEBUG("Failed to read table data for validation\n");
            free(data);
            return result;
        }
        
        uint32_t calculated_crc = calculate_crc32(data, header.data_len);
        if (calculated_crc != header.data_crc) {
            TRACE_DEBUG("Data CRC mismatch for table '%s'\n", table_name);
            free(data);
            return -1;
        }

        free(data);
        return 0;
    }

    return 0;
}

int fast_flash_repair_table(const char *table_name) {
    if (!table_name || !g_manager_loaded) {
        return -1;
    }

    // 对于NOR Flash，"修复"通常意味着重新计算CRC
    int idx = find_table_index(table_name);
    if (idx < 0) {
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];
    table_header_t header;

    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        return -1;
    }

    if (header.magic != MAGIC_NUMBER_TABLE) {
        return -1;
    }

    // 重新计算数据CRC
    if (header.data_len > 0) {
        uint8_t *data = malloc(header.data_len);
        if (!data) {
            return -1;
        }

        int result = g_flash_ops->read(table_info->addr + sizeof(header), data, header.data_len);
        if (result == 0) {
            header.data_crc = calculate_crc32(data, header.data_len);
            result = write_with_chunks(table_info->addr, (uint8_t*)&header, sizeof(header));
        }

        free(data);
        return result;
    }

    return 0;
}

// 新增：获取当前表写入的数据数量
uint32_t fast_flash_get_table_count(const char *table_name) {
    if (!table_name || !g_manager_loaded) {
        return 0;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return 0;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return 0;
    }

    return header.struct_nums;
}

// 新增：修改指定index的数据（只能修改已存在的数据）
int fast_flash_write_table_data_by_index(const char *table_name, uint32_t index, const void *data, uint32_t size) {
    if (!table_name || !data || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取当前表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return -1;
    }

    // 检查传入的数据大小是否与结构体大小匹配
    if (size != header.struct_size) {
        TRACE_DEBUG("Data size %u doesn't match table struct size %u for '%s'\n",
                   size, header.struct_size, table_name);
        return -1;
    }

    // 检查index是否在有效范围内（只能修改已存在的数据）
    if (index >= header.struct_nums) {
        TRACE_DEBUG("Index %u is out of range (current data count: %u) for table '%s'\n",
                   index, header.struct_nums, table_name);
        return -2;  // 表示超出已有数据范围
    }

    // 读取指定index的现有数据，检查是否与传入数据一致
    uint8_t *existing_data = malloc(header.struct_size);
    if (!existing_data) {
        TRACE_DEBUG("Memory allocation failed for reading existing data\n");
        return -1;
    }

    uint32_t data_offset = table_info->addr + sizeof(header) + index * header.struct_size;
    if (g_flash_ops->read(data_offset, existing_data, header.struct_size) != 0) {
        TRACE_DEBUG("Failed to read existing data at index %u\n", index);
        free(existing_data);
        return -1;
    }

    // 检查数据是否一致，如果一致则直接返回成功，避免不必要的写操作
    if (memcmp(existing_data, data, header.struct_size) == 0) {
        TRACE_DEBUG("Data at index %u is identical, no need to write\n", index);
        free(existing_data);
        return 0;
    }
    
    TRACE_DEBUG("Data at index %u is different, proceeding with write\n", index);
    free(existing_data);

    // 数据不一致，需要写入，读取所有现有数据
    uint8_t *all_data = malloc(header.data_len);
    if (!all_data) {
        TRACE_DEBUG("Memory allocation failed for table '%s'\n", table_name);
        return -1;
    }

    // 读取现有的所有数据
    if (g_flash_ops->read(table_info->addr + sizeof(header), all_data, header.data_len) != 0) {
        TRACE_DEBUG("Failed to read existing data for table '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    // 修改指定位置的数据（在读取的数据中直接替换）
    uint32_t offset = index * header.struct_size;
    memcpy(all_data + offset, data, size);

    // 更新表头的CRC
    header.data_crc = calculate_crc32(all_data, header.data_len);

    // 分配新的表空间（写入修改后的数据）
    uint32_t new_table_addr;
    int result = allocate_table_space(header.data_len+sizeof(table_header_t), &new_table_addr);
    if (result != 0) {
        TRACE_DEBUG("Failed to allocate space for modified table '%s'\n", table_name);
        free(all_data);
        return result;
    }

    // 写入新表头
    if (write_with_chunks(new_table_addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to write updated table header for '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    // 写入修改后的所有数据
    if (write_with_chunks(new_table_addr + sizeof(header), all_data, header.data_len) != 0) {
        TRACE_DEBUG("Failed to write modified table data for '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    free(all_data);

    // 更新管理表信息（指向新的表位置）
    table_info->addr = new_table_addr;
    table_info->used_size = sizeof(header) + header.data_len;

    // 保存管理表
    result = save_manager_table();
    if (result != 0) {
        TRACE_DEBUG("Failed to save manager table after modifying '%s'\n", table_name);
        return result;
    }

    TRACE_DEBUG("Modified data in table '%s' at index %u\n", table_name, index);
    return 0;
}

// 新增：累加数据，基于max_structs管控
int fast_flash_append_table_data(const char *table_name, const void *data, uint32_t size) {
    if (!table_name || !data || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取当前表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return -1;
    }

    // 检查传入的数据大小是否与结构体大小匹配
    if (size != header.struct_size) {
        TRACE_DEBUG("Data size %u doesn't match table struct size %u for '%s'\n",
                   size, header.struct_size, table_name);
        return -1;
    }

    // 计算最大允许的结构体数量
    uint32_t max_structs = (header.table_size-sizeof(table_header_t)) / header.struct_size;

    // 检查是否还有空间添加新数据
    if (header.struct_nums >= max_structs) {
        TRACE_DEBUG("Table '%s' is full (current: %u, max: %u)\n",
                   table_name, header.struct_nums, max_structs);
        return -2;  // 表已满
    }

    return fast_flash_write_table_data(table_name, data, size);
}

// 新增：清除指定mask标记的数据，保证索引连续
int fast_flash_clear_table_data(const char *table_name, uint64_t clear_mask) {
    if (!table_name || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取当前表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return -1;
    }

    // 检查清除掩码是否有效（不能清除不存在的索引）
    uint64_t max_mask = (header.struct_nums < 64) ? ((1ULL << header.struct_nums) - 1) : 0xFFFFFFFFFFFFFFFF;
    if ((clear_mask & ~max_mask) != 0) {
        TRACE_DEBUG("Clear mask 0x%016llX contains invalid bits (max: 0x%016llX) for table '%s'\n", 
                   clear_mask, max_mask, table_name);
        return -2;
    }

    // 如果没有需要清除的数据，直接返回成功
    if (clear_mask == 0) {
        TRACE_DEBUG("No data to clear for table '%s' (mask: 0x%016llX)\n", table_name, clear_mask);
        return 0;
    }

    // 执行清除操作
    TRACE_DEBUG("Clearing data with mask 0x%016llX for table '%s'\n", clear_mask, table_name);
    
    // 读取所有数据
    uint8_t *all_data = malloc(header.data_len);
    if (!all_data) {
        TRACE_DEBUG("Memory allocation failed for table '%s'\n", table_name);
        return -1;
    }

    if (g_flash_ops->read(table_info->addr + sizeof(table_header_t), all_data, header.data_len) != 0) {
        TRACE_DEBUG("Failed to read existing data for table '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    // 创建新的数据缓冲区，跳过被标记清除的数据
    uint32_t new_data_len = 0;
    uint8_t *new_data = malloc(header.data_len);  // 新数据不会比原数据长
    if (!new_data) {
        TRACE_DEBUG("Memory allocation failed for new data buffer\n");
        free(all_data);
        return -1;
    }

    uint32_t new_struct_nums = 0;
    for (uint32_t i = 0; i < header.struct_nums; i++) {
        if (!(clear_mask & (1ULL << i))) {
            // 该index没有被标记清除，复制数据
            memcpy(new_data + new_data_len,
                   all_data + i * header.struct_size,
                   header.struct_size);
            new_data_len += header.struct_size;
            new_struct_nums++;
        }
    }

    // 更新表头信息
    header.data_len = new_data_len;
    header.struct_nums = new_struct_nums;

    // 更新CRC校验值
    if (new_data_len > 0) {
        header.data_crc = calculate_crc32(new_data, new_data_len);
    } else {
        header.data_crc = 0;
    }

    // 分配新的表空间
    uint32_t new_table_addr;
    int result = allocate_table_space(sizeof(table_header_t) + new_data_len, &new_table_addr);
    if (result != 0) {
        TRACE_DEBUG("Failed to allocate space for cleared table '%s'\n", table_name);
        free(all_data);
        free(new_data);
        return result;
    }

    // 写入新表头
    if (write_with_chunks(new_table_addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to write new header for table '%s'\n", table_name);
        free(all_data);
        free(new_data);
        return -1;
    }

    // 写入新数据
    if (new_data_len > 0) {
        if (write_with_chunks(new_table_addr + sizeof(header), new_data, new_data_len) != 0) {
            TRACE_DEBUG("Failed to write new data for table '%s'\n", table_name);
            free(all_data);
            free(new_data);
            return -1;
        }
    }

    // 更新管理表
    table_info->addr = new_table_addr;
    table_info->size = sizeof(table_header_t) + new_data_len;

    result = save_manager_table();
    if (result != 0) {
        TRACE_DEBUG("Failed to save manager table after clearing '%s'\n", table_name);
        free(all_data);
        free(new_data);
        return result;
    }

    free(all_data);
    free(new_data);

    TRACE_DEBUG("Cleared data from table '%s', new struct count: %u\n", table_name, new_struct_nums);
    return 0;
}

// 新增：批量写入数据，避免频繁构建新表
int fast_flash_write_table_data_batch(const char *table_name, const void *data, uint32_t struct_size, uint32_t count) {
    if (!table_name || !data || count == 0 || !g_manager_loaded) {
        return -1;
    }

    int idx = find_table_index(table_name);
    if (idx < 0) {
        TRACE_DEBUG("Table '%s' not found\n", table_name);
        return -1;
    }

    flash_table_info_t *table_info = &g_manager_table.tables[idx];

    // 读取当前表头获取结构信息
    table_header_t header;
    if (g_flash_ops->read(table_info->addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to read table header for '%s'\n", table_name);
        return -1;
    }

    // 检查传入的数据大小是否与结构体大小匹配
    if (struct_size != header.struct_size) {
        TRACE_DEBUG("Data struct size %u doesn't match table struct size %u for '%s'\n",
                   struct_size, header.struct_size, table_name);
        return -1;
    }

    // 计算新的数据长度（在现有数据基础上添加批量数据）
    uint32_t total_data_size = struct_size * count;
    uint32_t new_data_len = header.data_len + total_data_size;

    // 检查是否超过表的最大容量
    uint32_t max_structs = (header.table_size - sizeof(table_header_t)) / header.struct_size;
    if (header.struct_nums + count > max_structs) {
        TRACE_DEBUG("Batch write exceeds table capacity: current=%u, adding=%u, max=%u for '%s'\n",
                   header.struct_nums, count, max_structs, table_name);
        return -2;  // 超出容量
    }

    // 分配新的表空间（表头 + 新的数据）
    uint32_t new_table_addr;
    int result = allocate_table_space(sizeof(table_header_t) + new_data_len, &new_table_addr);
    if (result != 0) {
        TRACE_DEBUG("Failed to allocate space for expanded table '%s' (batch write)\n", table_name);
        return result;
    }

    // 分配临时缓冲区用于存储所有数据（旧数据 + 新批量数据）
    uint8_t *all_data = malloc(new_data_len);
    if (!all_data) {
        TRACE_DEBUG("Memory allocation failed for batch write to table '%s'\n", table_name);
        return -1;
    }

    // 读取旧数据（如果有的话）
    if (header.data_len > 0) {
        if (g_flash_ops->read(table_info->addr + sizeof(header), all_data, header.data_len) != 0) {
            TRACE_DEBUG("Failed to read old data for batch write to table '%s'\n", table_name);
            free(all_data);
            return -1;
        }
    }

    // 添加批量数据到缓冲区末尾
    memcpy(all_data + header.data_len, data, total_data_size);

    // 更新表头
    header.data_len = new_data_len;
    header.struct_nums = new_data_len / header.struct_size;
    header.data_crc = calculate_crc32(all_data, new_data_len);

    // 写入新表头
    if (write_with_chunks(new_table_addr, (uint8_t*)&header, sizeof(header)) != 0) {
        TRACE_DEBUG("Failed to write new table header for batch write to '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    // 写入所有数据
    if (write_with_chunks(new_table_addr + sizeof(header), all_data, new_data_len) != 0) {
        TRACE_DEBUG("Failed to write table data for batch write to '%s'\n", table_name);
        free(all_data);
        return -1;
    }

    free(all_data);

    // 更新管理表信息
    table_info->addr = new_table_addr;
    table_info->size = header.table_size;
    table_info->used_size = sizeof(table_header_t) + new_data_len;

    // 保存管理表
    result = save_manager_table();
    if (result != 0) {
        TRACE_DEBUG("Failed to save manager table after batch write to '%s'\n", table_name);
        return result;
    }

    TRACE_DEBUG("Batch write to table '%s': added %u items, new total size: %u bytes\n", 
               table_name, count, new_data_len);
    return 0;
}

