/**
 * flash_hal.h - 统一 Flash HAL 注册契约（平台无关）
 *
 * 设计目标：所有存储框架（KV / 文件系统 / 裸机配置 / 环形缓冲）通过
 * 同一个最小 Flash 操作抽象访问介质，目标平台只需实现 read/write/erase
 * 三个回调并填充几何参数（注册式），即可让框架跑起来，库本身不绑定任何
 * 具体驱动或操作系统。
 *
 * 本文件是"导出库包"的唯一平台依赖头：
 *   - 平台侧：实现 flash_hal_t 的三个回调（真实驱动），调用框架注册函数；
 *   - 模拟侧：simulator/flash_hal_adapter.c 提供 flash_hal_from_sim()
 *     把模拟基座（flash_sim）桥接为 flash_hal_t，供 PC 验证使用。
 *
 * 约定：
 *   - 回调返回 0 表示成功，负值表示失败（错误码见 flash_hal_err_t）。
 *   - read/write 可任意长度（驱动自行处理页/字节粒度）；
 *   - erase 的 offset 必须按 erase_size 对齐，len 必须为 erase_size 整数倍。
 *   - NOR/NAND 写入前区域须已擦除（仅允许 1->0）；EEPROM 无擦除概念，
 *     erase 可返回 -5（不支持）。
 */

#ifndef FLASH_HAL_H
#define FLASH_HAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 通用错误码（与多数嵌入式 Flash 驱动约定一致，0 表示成功） */
typedef enum {
    FLASH_HAL_OK          = 0,
    FLASH_HAL_ERR_ARGS    = -1, /* 参数非法 */
    FLASH_HAL_ERR_RANGE   = -2, /* 地址/长度越界 */
    FLASH_HAL_ERR_ERASE   = -3, /* 擦除失败（坏块/寿命耗尽） */
    FLASH_HAL_ERR_WRITE   = -4, /* 写入失败 */
    FLASH_HAL_ERR_NOTSUP  = -5, /* 操作不支持（如对 EEPROM 调用 erase） */
    FLASH_HAL_ERR_IO      = -6  /* 底层 IO 失败 */
} flash_hal_err_t;

/* 统一 Flash 操作抽象：目标平台注册的最小接口集合 */
typedef struct flash_hal flash_hal_t;
struct flash_hal {
    void       *ctx;         /* 驱动私有上下文（可 NULL） */
    uint32_t    total_size;  /* 介质总容量（字节） */
    uint32_t    erase_size;  /* 擦除块大小（字节），EEPROM 可 0 */
    uint32_t    write_size;  /* 最小写入单位（字节） */

    /* 读取 len 字节到 buf；返回 0 成功 / 负值失败 */
    int (*read)(void *ctx, uint32_t off, void *buf, uint32_t len);
    /* 写入 len 字节；NOR/NAND 前须已擦除；返回 0 成功 / 负值失败 */
    int (*write)(void *ctx, uint32_t off, const void *buf, uint32_t len);
    /* 按块擦除 [off, off+len)；off/len 须按 erase_size 对齐；返回 0 成功 / 负值失败 */
    int (*erase)(void *ctx, uint32_t off, uint32_t len);
};

#ifdef __cplusplus
}
#endif

#endif /* FLASH_HAL_H */
