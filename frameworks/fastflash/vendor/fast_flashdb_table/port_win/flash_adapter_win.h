#ifndef FLASH_ADAPTER_WIN_H
#define FLASH_ADAPTER_WIN_H

#include "../core/fast_flash_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Windows平台Flash适配器接口
extern const flash_ops_t win_flash_ops;

// Windows平台特定配置
#define WIN_FLASH_FILE_NAME    "flash_simulation.bin"
#define WIN_FLASH_TOTAL_SIZE   (64 * 1024)    // 64KB模拟Flash
#define WIN_FLASH_SECTOR_COUNT (WIN_FLASH_TOTAL_SIZE / FLASH_SECTOR_SIZE)

// Windows平台特定函数
int win_flash_init(void);
int win_flash_read(uint32_t addr, uint8_t *buf, uint32_t size);
int win_flash_write(uint32_t addr, const uint8_t *buf, uint32_t size);
int win_flash_erase(uint32_t addr, uint32_t size);

// 用于测试的辅助函数
int win_flash_reset(void);              // 重置整个Flash区域
int win_flash_dump(uint32_t addr, uint32_t size); // 调试输出Flash内容
uint32_t get_time_ms(void);
// 性能统计结构
typedef struct {
    uint32_t total_write_time_ms;      // 总写入时间（毫秒）
    uint32_t total_erase_time_ms;       // 总擦除时间（毫秒）
    uint32_t total_read_time_ms;       // 总读取时间（毫秒）
    uint32_t write_operations;          // 写入操作次数
    uint32_t erase_operations;         // 擦除操作次数
    uint32_t read_operations;          // 读取操作次数
    uint32_t bytes_written;            // 写入字节数
    uint32_t bytes_erased;             // 擦除字节数
    uint32_t bytes_read;               // 读取字节数
} win_flash_perf_stats_t;

// 性能统计函数
void win_flash_reset_perf_stats(void);
void win_flash_get_perf_stats(win_flash_perf_stats_t *stats);
void win_flash_print_perf_stats(void);

#ifdef __cplusplus
}
#endif

#endif // FLASH_ADAPTER_WIN_H