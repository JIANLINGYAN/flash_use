/**
 * nvdm_sim_port.c - Airoha NVDM 组件移植层：对接本平台模拟基座 flash_sim
 *
 * 定位：NVDM 核心库（vendor/）平台无关，仅通过 nvdm_port.h 声明的
 * nvdm_port_* 契约函数访问 Flash 硬件 / 内存 / 互斥 / 日志。本移植层
 * 是实现这些契约的唯一适配文件，vendor/ 源码零修改。
 *
 * 移植要点：
 *   - 分区：1 个分区，起始偏移 0，容量/擦除块/条目上限由 nvdm_sim_setup
 *     注入（须在 nvdm_init 之前调用）。
 *   - Flash 读写擦：直接桥接 flash_sim_read/write/erase；NVDM 的物理
 *     地址即介质绝对偏移（base_addr 恒为 0），故无需地址换算。
 *   - 互斥/任务：单线程仿真，空实现。
 *   - 日志：nvdm_msgid_log.h 声明的 nvdm_001~nvdm_133 字符串在此定义，
 *     nvdm_log_* 输出到 stdout（级别前缀 [I]/[W]/[E]）。
 */

#ifndef MTK_NVDM_ENABLE
#define MTK_NVDM_ENABLE
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nvdm_port.h"
#include "nvdm_internal.h"

#include "flash_hal.h"
#include "nvdm_sim_port.h"

/* NVDM 全局控制块（定义于 vendor/src/nvdm_main.c） */
extern nvdm_ctrl_block_t g_ncb;

/* ==================== 日志字符串定义 ==================== */
/* nvdm_msgid_log.h 声明了这些符号，须由移植层提供定义以完成链接。 */

const char nvdm_001[] = "data item header info show below:";
const char nvdm_002[] = "status: 0x%x";
const char nvdm_003[] = "pnum: %d";
const char nvdm_004[] = "offset: 0x%x";
const char nvdm_005[] = "sequence_number: %d";
const char nvdm_006[] = "group_name_size: %d";
const char nvdm_007[] = "data_item_name_size: %d";
const char nvdm_008[] = "value_size: %d";
const char nvdm_009[] = "index: %d";
const char nvdm_010[] = "type: %d";
const char nvdm_011[] = "hash_name: 0x%x";
const char nvdm_012[] = "hashname = 0x%x";
const char nvdm_013[] = "nvdm_read_data_item: begin to read";
const char nvdm_015[] = "nvdm_write_data_item: begin to write";
const char nvdm_016[] = "find_data_item_by_hashname return %d";
const char nvdm_017[] = "peb free space is not enough";
const char nvdm_018[] = "too many data items in nvdm region";
const char nvdm_019[] = "new data item append";
const char nvdm_020[] = "old data item overwrite";
const char nvdm_022[] = "nvdm_write_data_item_non_blocking: begin to write";
const char nvdm_023[] = "Can't alloc memory!!";
const char nvdm_024[] = "Can't send queue!!";
const char nvdm_025[] = "nvdm_delete_data_item: enter";
const char nvdm_026[] = "nvdm_delete_group: enter";
const char nvdm_027[] = "nvdm_delete_all: enter";
const char nvdm_028[] = "nvdm_query_begin: enter";
const char nvdm_029[] = "nvdm_query_end: enter";
const char nvdm_030[] = "nvdm_query_next_group_name: enter";
const char nvdm_031[] = "nvdm_query_next_data_item_name: enter";
const char nvdm_032[] = "nvdm_query_data_item_length: begin to query";
const char nvdm_034[] = "scanning pnum(%d) to analysis data item info";
const char nvdm_035[] = "pnum=%d, offset=0x%x";
const char nvdm_036[] = "Detect index of data item with out of range, max = %d, curr = %d";
const char nvdm_037[] = "detect checksum error";
const char nvdm_038[] = "too many data items in nvdm region";
const char nvdm_039[] = "detect two valid copy of data item";
const char nvdm_040[] = "copy1(pnum=%d, offset=0x%x), copy2(pnum=%d, offset=0x%x)";
const char nvdm_041[] = "abnormal_data_item = %d at %d block with offset 0x%x";
const char nvdm_042[] = "Max size of data item must less than or equal to 2048 bytes";
const char nvdm_043[] = "alloc data_item_headers fail";
const char nvdm_044[] = "old_src_pnum=%d, old_pos=0x%x, new_src_pnum=%d, new_pos=0x%x, item_size=%d";
const char nvdm_045[] = "Error item status(0x%x) at src_pnum=%d, pos=0x%x";
const char nvdm_046[] = "pnum=%d, offset=0x%x, len=%d";
const char nvdm_047[] = "addr=0x%x, pnum=%d, offset=0x%x, len=%d";
const char nvdm_048[] = "pnum=%d";
const char nvdm_049[] = "region info show below:";
const char nvdm_050[] = "peb    valid    free    dirty    erase_count    is_reserved";
const char nvdm_051[] = "%3d     %4d    %4d     %4d       %8d              %d";
const char nvdm_052[] = "valid_data_size = %d";
const char nvdm_053[] = "peb header(%d) info show below:";
const char nvdm_054[] = "magic: %x";
const char nvdm_055[] = "erase_count: %x";
const char nvdm_056[] = "status: %x";
const char nvdm_057[] = "peb_reserved: %x";
const char nvdm_058[] = "version: %x";
const char nvdm_059[] = "pnum=%d";
const char nvdm_060[] = "pnum=%d";
const char nvdm_061[] = "offset=0x%x";
const char nvdm_062[] = "len=%d";
const char nvdm_063[] = "magic=0x%x, erase_count=0x%x, status=0x%x, peb_reserved=0x%x";
const char nvdm_064[] = "pnum=%d";
const char nvdm_065[] = "offset=0x%x";
const char nvdm_066[] = "len=%d";
const char nvdm_067[] = "pnum=%d";
const char nvdm_068[] = "pnum=%d";
const char nvdm_069[] = "pnum=%d";
const char nvdm_070[] = "pnum=%d";
const char nvdm_071[] = "pnum=%d";
const char nvdm_072[] = "found no valid data in reclaiming pebs when relocate_pebs()";
const char nvdm_073[] = "target_peb=%d";
const char nvdm_074[] = "found a target peb(%d) for reclaiming";
const char nvdm_075[] = "merge peb: %d, data_size: %d";
const char nvdm_076[] = "start garbage collection!!!";
const char nvdm_077[] = "GC buffer alloc fail";
const char nvdm_078[] = "non_reserved_pebs = %d";
const char nvdm_079[] = "mean_erase_count = %d";
const char nvdm_080[] = "reclaim blocks select by erase count = %d";
const char nvdm_081[] = "reclaim peb_list(no-sort): ";
const char nvdm_082[] = "%d";
const char nvdm_083[] = "reclaim peb_list(sort): ";
const char nvdm_084[] = "%d";
const char nvdm_085[] = "reclaim blocks select by valid size = %d";
const char nvdm_086[] = "reclaim peb_list(no-sort): ";
const char nvdm_087[] = "%d";
const char nvdm_088[] = "reclaim peb_list(sort): ";
const char nvdm_089[] = "%d";
const char nvdm_090[] = "find_free_peb: target_peb = %d, reserved_peb = %d, reserved_peb_cnt = %d";
const char nvdm_091[] = "config information: [IS]: %d  [GN]: %d  [IN]: %d  [IC]: %d  [PS]: %d  [PC]: %d  [AS]: %d";
const char nvdm_092[] = "space_is_enough: valid_data_size = %d, new add size = %d";
const char nvdm_093[] = "detect valid_data_size abnormal";
const char nvdm_094[] = "reclaiming_peb alloc fail";
const char nvdm_095[] = "scan and verify peb headers";
const char nvdm_096[] = "before verify peb header";
const char nvdm_097[] = "peb_header validate fail, pnum=%d";
const char nvdm_098[] = "find more than one transfering peb, first=%d, second=%d";
const char nvdm_099[] = "find more than one transfered peb, first=%d, second=%d";
const char nvdm_100[] = "peb_header validate fail, pnum=%d";
const char nvdm_101[] = "peb_header validate fail, pnum=%d";
const char nvdm_102[] = "after verify peb header";
const char nvdm_103[] = "transfering_peb = %d";
const char nvdm_104[] = "transfered_peb = %d";
const char nvdm_105[] = "reclaiming_peb[%d] = %d";
const char nvdm_106[] = "update erase count for unknown pebs";
const char nvdm_107[] = "scan all non-reserved pebs including reclaiming pebs and transfering peb";
const char nvdm_108[] = "found a peb in transfering status";
const char nvdm_109[] = "found a peb in transfered status";
const char nvdm_110[] = "reclaim_idx=%d, transfered_peb=%d, transfering_peb=%d";
const char nvdm_111[] = "calculate total valid data size";
const char nvdm_112[] = "Count of PEB for NVDM region must greater than or equal to 2";
const char nvdm_113[] = "alloc peb_info fail";
const char nvdm_114[] = "nvdm init finished";
const char nvdm_115[] = "garbage collection finished, about %d ms used";
const char nvdm_116[] = "invalid data item at (%d, %d) with %d bytes( %d, %d )";
const char nvdm_117[] = "Canceled %d non-blocking write data";
const char nvdm_118[] = "[%c] Receive null pointer";
const char nvdm_119[] = "[%c] Size dismatch(%u, %u)";
const char nvdm_120[] = "[%c] Length dismatch: G(%u, %u), I(%u, %u)";
const char nvdm_121[] = "[%c] The NVDM driver has not been initialized.";
const char nvdm_122[] = "Unsupported data type(%d).";
const char nvdm_123[] = "Skip %u partition because of cfg dismatch.";
const char nvdm_124[] = "[NBW_Cancel] next ptr is 0x%x and dummy head is 0x%x";
const char nvdm_125[] = "partition %u need %u byte for %u item headers.";
const char nvdm_126[] = "partition %u need %u byte for %u PEB headers.";
const char nvdm_127[] = "NVDM partition[%d] info: [F]%u [D]%u [V]%u [C]%u [P]%u";
const char nvdm_128[] = "Actively trigger GC with { %d, %d }.";
const char nvdm_129[] = "Actively trigger GC done { %d, %d, %d ms }.";
const char nvdm_130[] = "malloc fail %d";
const char nvdm_131[] = "cur_item_count:%d";
const char nvdm_132[] = "!!!!!!nvdm item count is exceed 90% of max, user need increase the max data item count!!!!!!";
const char nvdm_133[] = "!!!!!!nvdm size is less than %d byte, user need increase partition size!!!!!!";

/* ==================== 日志函数 ==================== */

static void nvdm_log_out(const char *level, const char *msg, va_list ap)
{
    if (msg == NULL || msg[0] == '\0') {
        return;
    }
    printf("[NVDM][%s] ", level);
    vprintf(msg, ap);
    printf("\n");
}

void nvdm_log_info(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    nvdm_log_out("I", message, ap);
    va_end(ap);
}

void nvdm_log_warning(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    nvdm_log_out("W", message, ap);
    va_end(ap);
}

void nvdm_log_error(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    nvdm_log_out("E", message, ap);
    va_end(ap);
}

void nvdm_log_msgid_info(const char *message, uint32_t arg_cnt, ...)
{
    va_list ap;
    (void)arg_cnt;
    va_start(ap, arg_cnt);
    nvdm_log_out("I", message, ap);
    va_end(ap);
}

void nvdm_log_msgid_warning(const char *message, uint32_t arg_cnt, ...)
{
    va_list ap;
    (void)arg_cnt;
    va_start(ap, arg_cnt);
    nvdm_log_out("W", message, ap);
    va_end(ap);
}

void nvdm_log_msgid_error(const char *message, uint32_t arg_cnt, ...)
{
    va_list ap;
    (void)arg_cnt;
    va_start(ap, arg_cnt);
    nvdm_log_out("E", message, ap);
    va_end(ap);
}

/* ==================== 分区配置 ==================== */

static const flash_hal_t *s_hal = NULL;
static uint32_t s_base_addr = 0;
static uint32_t s_peb_size = 4096;
static uint32_t s_peb_count = 0;
static uint32_t s_item_count = 100;

static nvdm_partition_cfg_t s_partition_cfg[1];

void nvdm_sim_setup(const flash_hal_t *hal, uint32_t base_addr, uint32_t capacity,
                    uint32_t peb_size, uint32_t item_count)
{
    s_hal = hal;
    s_base_addr = base_addr;
    s_peb_size = peb_size;
    s_peb_count = capacity / peb_size;
    s_item_count = item_count;
}

void nvdm_sim_reset(void)
{
    memset(&g_ncb, 0, sizeof(g_ncb));
}

void *nvdm_sim_device(void)
{
    return NULL;   /* 已改为注册式 HAL，介质句柄由测试程序持有 */
}

nvdm_partition_cfg_t *nvdm_port_load_partition_info(uint32_t *partition_num)
{
    *partition_num = 0;
    if (s_hal == NULL) {
        return s_partition_cfg;
    }
    if (s_peb_count < 2u) {
        nvdm_log_error("nvdm_port_load_partition_info: peb_count < 2 (%u)", s_peb_count);
        return s_partition_cfg;
    }
    s_partition_cfg[0].base_addr = s_base_addr;
    s_partition_cfg[0].peb_size = s_peb_size;
    s_partition_cfg[0].peb_count = s_peb_count;
    s_partition_cfg[0].max_item_size = 1024;
    s_partition_cfg[0].max_group_name_size = 16;
    s_partition_cfg[0].max_item_name_size = 32;
    s_partition_cfg[0].total_item_count = s_item_count;
    *partition_num = 1;
    return s_partition_cfg;
}

bool nvdm_port_get_max_item_cfg(nvdm_partition_cfg_t *p_cfg)
{
    if (p_cfg == NULL) {
        return false;
    }
    p_cfg->base_addr = s_base_addr;
    p_cfg->peb_size = s_peb_size;
    p_cfg->peb_count = s_peb_count;
    p_cfg->max_item_size = 1024;
    p_cfg->max_group_name_size = 16;
    p_cfg->max_item_name_size = 32;
    p_cfg->total_item_count = s_item_count;
    return true;
}

/* ==================== Flash 读写擦 ==================== */

void nvdm_port_flash_read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    if (s_hal == NULL || buffer == NULL) {
        nvdm_port_must_assert();
        return;
    }
    if (s_hal->read(s_hal->ctx, address, buffer, length) != 0) {
        nvdm_log_error("flash read fail: addr=0x%x len=%u", address, length);
        nvdm_port_must_assert();
    }
}

void nvdm_port_flash_write(uint32_t address, const uint8_t *buffer, uint32_t length)
{
    if (s_hal == NULL || buffer == NULL) {
        nvdm_port_must_assert();
        return;
    }
    if (s_hal->write(s_hal->ctx, address, buffer, length) != 0) {
        nvdm_log_error("flash write fail: addr=0x%x len=%u", address, length);
        nvdm_port_must_assert();
    }
}

void nvdm_port_flash_erase(uint32_t address)
{
    if (s_hal == NULL) {
        nvdm_port_must_assert();
        return;
    }
    if (s_hal->erase(s_hal->ctx, address, s_peb_size) != 0) {
        nvdm_log_error("flash erase fail: addr=0x%x size=%u", address, s_peb_size);
        nvdm_port_must_assert();
    }
}

uint32_t nvdm_port_get_peb_address(uint32_t partition, int32_t pnum, int32_t offset)
{
    if (partition >= 1u || pnum < 0 || (uint32_t)pnum >= s_peb_count ||
        offset < 0 || (uint32_t)offset >= s_peb_size) {
        nvdm_log_error("invalid peb access: part=%u pnum=%d offset=%d", partition, pnum, offset);
        nvdm_port_must_assert();
        return 0xDEADBEEF;
    }
    return s_base_addr + (uint32_t)pnum * s_peb_size + (uint32_t)offset;
}

/* ==================== 内存 / 互斥 / 任务 ==================== */

void *nvdm_port_malloc(uint32_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        nvdm_log_error("malloc fail %u", size);
    }
    return p;
}

void nvdm_port_free(void *pdata)
{
    free(pdata);
}

void nvdm_port_mutex_creat(void) { }
void nvdm_port_mutex_take(void) { }
void nvdm_port_mutex_give(void) { }

void nvdm_port_protect_mutex_create(void) { }
void nvdm_port_protect_mutex_take(void) { }
void nvdm_port_protect_mutex_give(void) { }

void nvdm_port_get_task_handler(void) { }
void nvdm_port_reset_task_handler(void) { }

bool nvdm_port_query_task_handler(void)
{
    return true;
}

const char *nvdm_port_get_curr_task_name(void)
{
    return "main";
}

bool nvdm_port_send_queue(void)
{
    return false;
}

bool nvdm_request_gc_in_daemon(const void *para)
{
    (void)para;
    return false;
}

/* ==================== 断言 / 计时 / 掉电 ==================== */

void nvdm_port_must_assert(void)
{
    assert(0);
}

void nvdm_port_poweroff_time_set(void) { }

void nvdm_port_poweroff(uint32_t poweroff_time)
{
    (void)poweroff_time;
}

static uint32_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000u);
}

uint32_t nvdm_port_get_count(void)
{
    return now_us();
}

uint32_t nvdm_port_get_duration_time(uint32_t begin, uint32_t end)
{
    return (uint32_t)(end - begin);
}
