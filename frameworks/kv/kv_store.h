/**
 * kv_store.h - 简易 KV 存储逻辑框架（平台无关）
 *
 * 设计定位：裸机小数据 / 高频配置场景的最小可用存储层。
 *  - 通过 flash_hal.h 的统一注册接口（flash_hal_t）访问底层介质
 *    （NOR/EEPROM 均可），不绑定任何平台。
 *  - 采用 "页内追加写 + 末尾状态字" 的两步提交，保证掉电安全：
 *      写入数据后仅当末尾状态字被写为 COMMITTED 才算生效，
 *      中途掉电（状态仍为 PENDING）的记录在加载时被丢弃。
 *  - 读取时全量扫描建立 key->offset 索引，后写覆盖前写（简化磨损均衡版）。
 *
 * 平台无关：仅依赖 C99 标准库与 flash_hal.h；目标平台只需实现
 * flash_hal_t 的 read/write/erase 回调并注册给 kv_init 即可使用。
 */

#ifndef KV_STORE_H
#define KV_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* KV 记录 Magic（用于识别合法记录头） */
#define KV_MAGIC       0x4B565344u /* "KVSD" */

/* 单条记录最大 value 长度（含头部须落在单块内） */
#define KV_MAX_VALUE   256u

/* 记录状态字（位于记录末尾，单独写入以实现两步提交） */
typedef enum {
    KV_STATE_ERASED  = 0xFF, /* 空（未写过，全 1） */
    KV_STATE_PENDING = 0x00, /* 已写数据，待提交 */
    KV_STATE_COMMITTED = 0x5A /* 已提交生效 */
} kv_state_t;

/* 单条 KV 记录头部（定长，紧接 value 后是一个状态字节） */
typedef struct {
    uint32_t magic;     /* KV_MAGIC */
    uint16_t key_id;    /* 键标识（应用层分配，0 保留） */
    uint16_t len;       /* value 实际长度（字节） */
    uint32_t crc;       /* value 的 CRC32（仅对 value 计算） */
} kv_header_t;

/**
 * 初始化 KV 存储区。
 * 要求 hal 已注册（read/write/erase 回调 + 几何参数）；KV 将使用
 * [base, base+size) 区域。
 * 首次使用会扫描已有记录建立索引；区域须为整块擦除态更优（NOR）。
 * @param hal   flash_hal_t 注册实例（目标平台实现回调）
 * @param base  KV 区域起始偏移（建议块对齐）
 * @param size  KV 区域大小（字节，建议为 erase_size 整数倍）
 * @return      0 成功；负值为错误码（flash_hal_err_t）
 */
int kv_init(const flash_hal_t *hal, uint32_t base, uint32_t size);

/**
 * 写入/更新一个 KV。
 * 采用两步提交：先写数据(PENDING)再写状态(COMMITTED)。
 * 若区域已满且无可回收空间，返回 FLASH_HAL_ERR_RANGE。
 */
int kv_write(const flash_hal_t *hal, uint16_t key_id,
             const void *value, uint16_t len);

/**
 * 读取一个 KV。
 * @param value  输出缓冲（至少 len 字节），为 NULL 时仅返回长度到 *len
 * @param len    in: value 缓冲容量；out: 实际 value 长度
 * @return       0 命中；FLASH_HAL_ERR_ARGS 未找到；其他为介质错误
 */
int kv_read(const flash_hal_t *hal, uint16_t key_id,
            void *value, uint16_t *len);

/**
 * 删除一个 KV（标记为无效，空间在下次 GC/重写时回收）。
 * 本简化版通过写入一条 key_id 相同、len=0 的 COMMITTED 记录实现覆盖失效。
 */
int kv_delete(const flash_hal_t *hal, uint16_t key_id);

/**
 * 返回当前已用（含历史无效记录）的字节数，用于磨损/容量观测。
 */
uint32_t kv_used_bytes(void);

/**
 * 统计（基于最近一次 kv_init 扫描）：有效条目数、历史写入记录数。
 */
typedef struct {
    uint32_t valid_entries; /* 当前可读 key 数 */
    uint32_t total_records; /* 区域中累计写入的记录数（含失效） */
} kv_summary_t;

int kv_summary(const flash_hal_t *hal, kv_summary_t *sum);

#ifdef __cplusplus
}
#endif

#endif /* KV_STORE_H */
