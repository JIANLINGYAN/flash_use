/*
 * spiffs_config.h - SPIFFS 运行配置（对接本平台模拟基座）
 *
 * 几何参数与模拟基座 NOR 介质一致：
 *   物理总容量 128KB、擦除块 4KB、页 256B（页=块内查找区+数据区）。
 * 配置宏用法参考 upstream 源码 src/default/spiffs_config.h。
 */
#ifndef SPIFFS_CONFIG_H_
#define SPIFFS_CONFIG_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* 编译期可调开关 */
#ifndef SPIFFS_DBG
#define SPIFFS_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_GC_DBG
#define SPIFFS_GC_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_CACHE_DBG
#define SPIFFS_CACHE_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_CHECK_DBG
#define SPIFFS_CHECK_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_API_DBG
#define SPIFFS_API_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_DBG
#define SPIFFS_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_LOCK_DBG
#define SPIFFS_LOCK_DBG(_f, ...) /*printf(_f, ## __VA_ARGS__)*/
#endif
#ifndef SPIFFS_TEST_VISUALISATION
#define SPIFFS_TEST_VISUALISATION 0
#endif

/* 性能与功能开关 */
#define SPIFFS_CACHE        1
#define SPIFFS_CACHE_WR     1
#define SPIFFS_CACHE_STATS  0
#define SPIFFS_GC_STATS     0

#define SPIFFS_USE_MAGIC            1
#define SPIFFS_USE_MAGIC_LENGTH     1
#define SPIFFS_ALIGNED_OBJECT_INDEX_TABLES 0

/* 调试格式宏（用于编译期拼接 printf 格式串） */
#ifndef _SPIPRIi
#define _SPIPRIi   "%d"
#endif
#ifndef _SPIPRIad
#define _SPIPRIad  "%08x"
#endif
#ifndef _SPIPRIbl
#define _SPIPRIbl  "%04x"
#endif
#ifndef _SPIPRIpg
#define _SPIPRIpg  "%04x"
#endif
#ifndef _SPIPRIsp
#define _SPIPRIsp  "%04x"
#endif
#ifndef _SPIPRIfd
#define _SPIPRIfd  "%d"
#endif
#ifndef _SPIPRIid
#define _SPIPRIid  "%04x"
#endif
#ifndef _SPIPRIfl
#define _SPIPRIfl  "%02x"
#endif
#ifndef _SPIPRIob
#define _SPIPRIob  "%04x"
#endif

/* 垃圾回收参数 */
#ifndef SPIFFS_GC_MAX_RUNS
#define SPIFFS_GC_MAX_RUNS          5
#endif
#ifndef SPIFFS_GC_HEUR_W_DELET
#define SPIFFS_GC_HEUR_W_DELET      (5)
#endif
#ifndef SPIFFS_GC_HEUR_W_USED
#define SPIFFS_GC_HEUR_W_USED       (-1)
#endif
#ifndef SPIFFS_GC_HEUR_W_ERASE_AGE
#define SPIFFS_GC_HEUR_W_ERASE_AGE  (50)
#endif

/* 栈上拷贝缓冲大小 */
#ifndef SPIFFS_COPY_BUFFER_STACK
#define SPIFFS_COPY_BUFFER_STACK    (64)
#endif

/* 锁宏（单线程环境为空） */
#ifndef SPIFFS_LOCK
#define SPIFFS_LOCK(fs)
#endif
#ifndef SPIFFS_UNLOCK
#define SPIFFS_UNLOCK(fs)
#endif

/* 功能开关 */
#ifndef SPIFFS_TEMPORAL_FD_CACHE
#define SPIFFS_TEMPORAL_FD_CACHE        1
#endif
#ifndef SPIFFS_TEMPORAL_CACHE_HIT_SCORE
#define SPIFFS_TEMPORAL_CACHE_HIT_SCORE 4
#endif
#ifndef SPIFFS_IX_MAP
#define SPIFFS_IX_MAP                   1
#endif

/* 文件名最大长度（含结尾 '\0'） */
#define SPIFFS_OBJ_NAME_LEN         32

/* 单例模式：回调不带 fs 参数 */
#define SPIFFS_SINGLETON            0

/* HAL 回调带 fs 参数 */
#define SPIFFS_HAL_CALLBACK_EXTRA   0

/* 文件句柄偏移 */
#define SPIFFS_FILEHDL_OFFSET       0

#define SPIFFS_READ_ONLY            0

/* 每块对象查找页数（编译期常量，用于静态分配查找缓冲）：
 * 块 4KB / 页 256B = 16 页/块；对象查找页 = MAX(1, 16*4/256) = 1，
 * 取 4 以满足任意小容量配置。 */
#ifndef SPIFFS_OBJ_LOOKUP_LEN
#define SPIFFS_OBJ_LOOKUP_LEN       4
#endif

/* 物理扇区大小 = 逻辑页大小 */
#ifndef SPIFFS_PH_SZ
#define SPIFFS_PH_SZ                256
#endif

/* 逻辑页大小 */
#ifndef SPIFFS_LOG_PAGE_SIZE
#define SPIFFS_LOG_PAGE_SIZE        256
#endif

/* 擦除块大小 */
#ifndef SPIFFS_BLOCK_SIZE
#define SPIFFS_BLOCK_SIZE           4096
#endif

/* 几何参数：通过宏暴露给 spiffs 内部（运行时以 mount 配置为准） */
#ifndef SPIFFS_CFG_PHYS_SZ
#define SPIFFS_CFG_PHYS_SZ(fs)      ((fs)->cfg.phys_size)
#endif
#ifndef SPIFFS_CFG_PHYS_ERASE_SZ
#define SPIFFS_CFG_PHYS_ERASE_SZ(fs) ((fs)->cfg.phys_erase_block)
#endif
#ifndef SPIFFS_CFG_PHYS_ADDR
#define SPIFFS_CFG_PHYS_ADDR(fs)    ((fs)->cfg.phys_addr)
#endif
#ifndef SPIFFS_CFG_LOG_PAGE_SZ
#define SPIFFS_CFG_LOG_PAGE_SZ(fs)  ((fs)->cfg.log_page_size)
#endif
#ifndef SPIFFS_CFG_LOG_BLOCK_SZ
#define SPIFFS_CFG_LOG_BLOCK_SZ(fs) ((fs)->cfg.log_block_size)
#endif

/* 内部基础类型 */
typedef int8_t  s8_t;
typedef uint8_t u8_t;
typedef int16_t s16_t;
typedef uint16_t u16_t;
typedef int32_t s32_t;
typedef uint32_t u32_t;

/* 索引类型（由配置决定，见 upstream default/spiffs_config.h 说明）：
 * block_ix 须容纳块数(128KB/4KB=32)，page_ix 须容纳页数(128KB/256B=512)，
 * 均可用 u16_t。spiffs_file/flags/mode/obj_type 由 spiffs.h 自行定义。 */
typedef uint16_t spiffs_block_ix;
typedef uint16_t spiffs_page_ix;
typedef uint16_t spiffs_obj_id;
typedef uint16_t spiffs_span_ix;

#endif /* SPIFFS_CONFIG_H_ */
