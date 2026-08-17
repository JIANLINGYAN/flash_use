#include "flash_adapter_win.h"
#include "../core/fast_flash_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

static FILE *flash_file = NULL;
static uint8_t flash_cache[WIN_FLASH_TOTAL_SIZE] = {0};  // 内存缓存，模拟Flash
static bool cache_dirty = false;

// 性能统计
static win_flash_perf_stats_t perf_stats = {0};

// Winbond Flash模拟参数（单位：毫秒）
#define WINBOND_WRITE_MIN_MS      0.7f
#define WINBOND_WRITE_MAX_MS      3.0f
#define WINBOND_ERASE_4K_MIN_MS   45.0f
#define WINBOND_ERASE_4K_MAX_MS   400.0f
#define WINBOND_ERASE_32K_MIN_MS  120.0f
#define WINBOND_ERASE_32K_MAX_MS  1600.0f
#define WINBOND_ERASE_64K_MIN_MS  150.0f
#define WINBOND_ERASE_64K_MAX_MS  2000.0f
#define READ_TIME_PER_BYTE_US     0.05f  // 每字节读取时间（微秒）

// 生成随机数在指定范围内的函数
static float random_range(float min_val, float max_val) {
    return min_val + ((float)rand() / RAND_MAX) * (max_val - min_val);
}

// 获取当前时间（毫秒）
uint32_t get_time_ms(void) {
    return GetTickCount64();
}

// 获取当前时间（微秒）
static uint64_t get_time_us(void) {
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000) / frequency.QuadPart;
}

// 模拟延时（毫秒）
static void sleep_ms(uint32_t ms) {
    Sleep(ms);
}

// 模拟延时（微秒）
static void sleep_us(uint64_t us) {
    if (us >= 1000) {
        Sleep(us / 1000);
    } else {
        // 对于小于1ms的延时，使用忙等待
        uint64_t start = get_time_us();
        while (get_time_us() - start < us) {
            // 忙等待
        }
    }
}

// 计算擦除时间
static float calculate_erase_time(uint32_t size) {
    if (size <= 4 * 1024) {
        return random_range(WINBOND_ERASE_4K_MIN_MS, WINBOND_ERASE_4K_MAX_MS);
    } else if (size <= 32 * 1024) {
        return random_range(WINBOND_ERASE_32K_MIN_MS, WINBOND_ERASE_32K_MAX_MS);
    } else {
        return random_range(WINBOND_ERASE_64K_MIN_MS, WINBOND_ERASE_64K_MAX_MS);
    }
}

// 加载Flash文件到缓存
static int load_flash_to_cache(void) {
    if (!flash_file) {
        return -1;
    }
    
    fseek(flash_file, 0, SEEK_SET);
    size_t read_size = fread(flash_cache, 1, WIN_FLASH_TOTAL_SIZE, flash_file);
    
    // 如果文件大小不足，填充0xFF（模拟Flash擦除状态）
    if (read_size < WIN_FLASH_TOTAL_SIZE) {
        memset(&flash_cache[read_size], 0xFF, WIN_FLASH_TOTAL_SIZE - read_size);
    }
    
    cache_dirty = false;
    return 0;
}

// 保存缓存到Flash文件
static int save_cache_to_flash(void) {
    if (!flash_file || !cache_dirty) {
        return 0;
    }
    
    fseek(flash_file, 0, SEEK_SET);
    size_t written = fwrite(flash_cache, 1, WIN_FLASH_TOTAL_SIZE, flash_file);
    fflush(flash_file);
    
    if (written == WIN_FLASH_TOTAL_SIZE) {
        cache_dirty = false;
        return 0;
    }
    return -1;
}

void win_flash_reset_perf_stats(void) {
    memset(&perf_stats, 0, sizeof(perf_stats));
}

void win_flash_get_perf_stats(win_flash_perf_stats_t *stats) {
    if (stats) {
        *stats = perf_stats;
    }
}

void win_flash_print_perf_stats(void) {
    printf("\n=== Flash Performance Statistics ===\n");
    printf("Write Operations: %u (Total: %u ms, Avg: %.2f ms)\n", 
           perf_stats.write_operations, perf_stats.total_write_time_ms,
           perf_stats.write_operations > 0 ? (float)perf_stats.total_write_time_ms / perf_stats.write_operations : 0.0f);
    printf("Erase Operations: %u (Total: %u ms, Avg: %.2f ms)\n", 
           perf_stats.erase_operations, perf_stats.total_erase_time_ms,
           perf_stats.erase_operations > 0 ? (float)perf_stats.total_erase_time_ms / perf_stats.erase_operations : 0.0f);
    printf("Read Operations: %u (Total: %u ms, Avg: %.2f ms)\n", 
           perf_stats.read_operations, perf_stats.total_read_time_ms,
           perf_stats.read_operations > 0 ? (float)perf_stats.total_read_time_ms / perf_stats.read_operations : 0.0f);
    printf("Bytes Written: %u (%.2f KB)\n", perf_stats.bytes_written, perf_stats.bytes_written / 1024.0f);
    printf("Bytes Erased: %u (%.2f KB)\n", perf_stats.bytes_erased, perf_stats.bytes_erased / 1024.0f);
    printf("Bytes Read: %u (%.2f KB)\n", perf_stats.bytes_read, perf_stats.bytes_read / 1024.0f);
    printf("Total Time: %u ms (%.2f seconds)\n", 
           perf_stats.total_write_time_ms + perf_stats.total_erase_time_ms + perf_stats.total_read_time_ms,
           (perf_stats.total_write_time_ms + perf_stats.total_erase_time_ms + perf_stats.total_read_time_ms) / 1000.0f);
    printf("===================================\n\n");
}

int win_flash_init(void) {
    // 初始化随机数种子
    srand((unsigned int)time(NULL));
    
    // 尝试打开现有文件，如果不存在则创建
    flash_file = fopen(WIN_FLASH_FILE_NAME, "rb+");
    if (!flash_file) {
        // 文件不存在，创建新文件
        flash_file = fopen(WIN_FLASH_FILE_NAME, "wb+");
        if (!flash_file) {
            printf("Failed to create flash simulation file: %s\n", WIN_FLASH_FILE_NAME);
            return -1;
        }
        
        // 初始化为全0xFF（模拟擦除状态）
        memset(flash_cache, 0xFF, WIN_FLASH_TOTAL_SIZE);
        fseek(flash_file, 0, SEEK_SET);
        fwrite(flash_cache, 1, WIN_FLASH_TOTAL_SIZE, flash_file);
        fflush(flash_file);
    } else {
        // 加载现有文件内容
        if (load_flash_to_cache() != 0) {
            printf("Failed to load flash file to cache\n");
            fclose(flash_file);
            flash_file = NULL;
            return -1;
        }
    }
    
    printf("Windows Flash Adapter initialized, file: %s\n", WIN_FLASH_FILE_NAME);
    printf("Flash simulation with performance timing enabled\n");
    printf("Write latency: %.1f-%.1f ms (Winbond spec)\n", WINBOND_WRITE_MIN_MS, WINBOND_WRITE_MAX_MS);
    printf("Erase 4KB: %.0f-%.0f ms\n", WINBOND_ERASE_4K_MIN_MS, WINBOND_ERASE_4K_MAX_MS);
    printf("Erase 32KB: %.0f-%.0f ms\n", WINBOND_ERASE_32K_MIN_MS, WINBOND_ERASE_32K_MAX_MS);
    printf("Erase 64KB: %.0f-%.0f ms\n", WINBOND_ERASE_64K_MIN_MS, WINBOND_ERASE_64K_MAX_MS);
    printf("\n");
    
    // 重置性能统计
    win_flash_reset_perf_stats();
    
    return 0;
}

int win_flash_read(uint32_t addr, uint8_t *buf, uint32_t size) {
    if (!flash_file || !buf) {
        return -1;
    }
    
    if (addr + size > WIN_FLASH_TOTAL_SIZE) {
        printf("Read out of bounds: addr=0x%08X, size=%u\n", addr, size);
        return -1;
    }
    
    // 确保缓存是最新的
    if (cache_dirty) {
        save_cache_to_flash();
        load_flash_to_cache();
    }
    
    // 模拟读取时间
    uint64_t start_time = get_time_us();
    memcpy(buf, &flash_cache[addr], size);
    uint64_t read_time_us = (uint64_t)(size * READ_TIME_PER_BYTE_US);
    sleep_us(read_time_us);
    uint64_t end_time = get_time_us();
    
    // 更新统计
    perf_stats.read_operations++;
    perf_stats.bytes_read += size;
    perf_stats.total_read_time_ms += (uint32_t)((end_time - start_time) / 1000);
    
    TRACE_DEBUG("Flash read: addr=0x%08X, size=%u, time=%llu us\n", addr, size, end_time - start_time);
    
    return 0;
}

int win_flash_write(uint32_t addr, const uint8_t *buf, uint32_t size) {
    if (!flash_file || !buf) {
        return -1;
    }
    
    if (addr + size > WIN_FLASH_TOTAL_SIZE) {
        printf("Write out of bounds: addr=0x%08X, size=%u\n", addr, size);
        return -1;
    }
    
    // 模拟写入时间（每个写操作都有随机延迟）
    uint32_t start_time = get_time_ms();
    float write_delay_ms = random_range(WINBOND_WRITE_MIN_MS, WINBOND_WRITE_MAX_MS);
    sleep_ms((uint32_t)write_delay_ms);
    
    // 模拟NOR Flash特性：只能将1写成0，不能将0写成1
    for (uint32_t i = 0; i < size; i++) {
        uint8_t old_val = flash_cache[addr + i];
        uint8_t new_val = buf[i];
        
        // 如果需要将0改成1，这是不允许的
        if ((old_val & new_val) != new_val) {
            printf("Flash write error: cannot change 0 to 1 at addr=0x%08X\n", addr + i);
            return -1;
        }
        
        flash_cache[addr + i] = new_val;
    }
    
    cache_dirty = true;
    save_cache_to_flash();
    load_flash_to_cache();
    
    uint32_t end_time = get_time_ms();
    
    // 更新统计
    perf_stats.write_operations++;
    perf_stats.bytes_written += size;
    perf_stats.total_write_time_ms += (end_time - start_time);
    
    printf("Flash write: addr=0x%08X, size=%u, time=%u ms (simulated %.1f ms)\n", 
           addr, size, end_time - start_time, write_delay_ms);
    
    return 0;
}

int win_flash_erase(uint32_t addr, uint32_t size) {
    if (!flash_file) {
        return -1;
    }
    
    if (addr + size > WIN_FLASH_TOTAL_SIZE) {
        printf("Erase out of bounds: addr=0x%08X, size=%u\n", addr, size);
        return -1;
    }
    
    // 对齐到扇区边界
    uint32_t aligned_addr = (addr / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    uint32_t aligned_size = ((size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    
    if (aligned_addr + aligned_size > WIN_FLASH_TOTAL_SIZE) {
        printf("Aligned erase out of bounds\n");
        return -1;
    }
    
    // 模拟擦除时间
    uint32_t start_time = get_time_ms();
    float erase_delay_ms = calculate_erase_time(aligned_size);
    sleep_ms((uint32_t)erase_delay_ms);
    
    // 擦除：设置为全0xFF
    memset(&flash_cache[aligned_addr], 0xFF, aligned_size);
    cache_dirty = true;
    
    uint32_t end_time = get_time_ms();
    
    // 更新统计
    perf_stats.erase_operations++;
    perf_stats.bytes_erased += aligned_size;
    perf_stats.total_erase_time_ms += (end_time - start_time);
    
    printf("Flash erase: addr=0x%08X, size=%u, time=%u ms (simulated %.1f ms)\n", 
           aligned_addr, aligned_size, end_time - start_time, erase_delay_ms);
    
    return 0;
}

int win_flash_reset(void) {
    // 确保Flash文件已初始化
    if (!flash_file) {
        if (win_flash_init() != 0) {
            return -1;
        }
    }
    
    uint32_t start_time = get_time_ms();
    
    memset(flash_cache, 0xFF, WIN_FLASH_TOTAL_SIZE);
    cache_dirty = true;
    save_cache_to_flash();
    
    uint32_t end_time = get_time_ms();
    
    printf("Flash reset completed in %u ms\n", end_time - start_time);
    return 0;
}

int win_flash_dump(uint32_t addr, uint32_t size) {
    if (!flash_file) {
        return -1;
    }
    
    if (addr + size > WIN_FLASH_TOTAL_SIZE) {
        printf("Dump out of bounds\n");
        return -1;
    }
    
    printf("Flash dump from 0x%08X, size: %u\n", addr, size);
    for (uint32_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            printf("\n%08X: ", addr + i);
        }
        printf("%02X ", flash_cache[addr + i]);
    }
    printf("\n");
    
    return 0;
}

// 在程序退出时自动保存
static void cleanup_flash(void) {
    if (flash_file) {
        save_cache_to_flash();
        fclose(flash_file);
        flash_file = NULL;
        
        // 打印最终的性能统计
        win_flash_print_perf_stats();
    }
}

// 注册清理函数
static void __attribute__((constructor)) register_cleanup(void) {
    atexit(cleanup_flash);
}

// Flash操作接口
const flash_ops_t win_flash_ops = {
    .init  = win_flash_init,
    .read  = win_flash_read,
    .write = win_flash_write,
    .erase = win_flash_erase
};