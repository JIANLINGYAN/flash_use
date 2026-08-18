/**
 * fs_store.h - 简易文件系统逻辑框架（基于模拟基座）
 *
 * 设计定位：裸机小容量数据 / 多文件管理场景的最小可用存储层。
 *  - 通过 flash_hal.h 的统一注册接口访问底层介质（推荐 NOR）。
 *  - 布局：介质被划分为等大块（块大小 = 擦除块）。块 0 存放文件分配表
 *    （FAT），其余为数据块。FAT 记录每个文件的起始块、块数与字节数。
 *  - 覆盖写（fs_write）：若新数据可放入已分配块，则原地擦写复用，适合
 *    "某文件频繁修改"场景；若超出，则整体迁移到新空闲块（旧块变孤儿，
 *    供后续分配复用）。
 *  - 追加写（fs_append）：优先利用文件末尾的空闲字节（擦除态 0xFF 内
 *    1->0 编程合法），无需整文件迁移。
 *  - 掉电模型：FAT 单独一块、后写提交。fs_write 扩展迁移时先写新数据块、
 *    再更新 FAT（两步提交），中途掉电旧 FAT 仍指向旧数据，保证数据一致。
 *    原地覆盖写不做原子性保证（裸机覆盖写语义，文档注明）。
 *
 * 平台无关：仅依赖 C99 标准库与 flash_hal.h；目标平台实现 flash_hal_t
 * 的 read/write/erase 回调并注册给 fs_init 即可使用。
 */

#ifndef FS_STORE_H
#define FS_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "flash_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 文件分配表 Magic（FAT 块起始标记："FSAT"） */
#define FS_FAT_MAGIC   0x46534154u

/* 文件名最大长度（含结尾 '\0'） */
#define FS_NAME_MAX    16u

/* 文件系统错误码（0 成功，负值错误） */
typedef enum {
    FS_OK        = 0,
    FS_ERR_ARGS  = -1,  /* 参数非法（name 空/超长、越界等） */
    FS_ERR_NOSPC = -2,  /* 数据块/文件槽不足 */
    FS_ERR_NOTFOUND = -3, /* 文件不存在 */
    FS_ERR_EXIST = -4,  /* 同名文件已存在 */
    FS_ERR_IO    = -5,  /* 底层介质读写/擦除失败 */
    FS_ERR_CORRUPT = -6 /* FAT 数据损坏（magic 不匹配） */
} fs_err_t;

/**
 * 初始化文件系统（加载/创建 FAT）。
 * 要求 hal 已注册（read/write/erase）；fs 将使用 [base, base+size) 区域。
 * @param hal        统一 HAL 注册实例
 * @param base       FS 区域起始偏移（须块对齐）
 * @param size       FS 区域大小（须为 block_size 整数倍，至少 2 块）
 * @param block_size 块大小（须等于底层 erase_size，如 NOR 4096）
 * @return           FS_OK 成功；FS_ERR_CORRUPT FAT 损坏（可 fs_format 重建）
 */
fs_err_t fs_init(const flash_hal_t *hal, uint32_t base, uint32_t size,
                 uint32_t block_size);

/**
 * 格式化：擦除整个 FS 区域并重建空 FAT。返回 FS_OK。
 */
fs_err_t fs_format(const flash_hal_t *hal, uint32_t base, uint32_t size,
                   uint32_t block_size);

/**
 * 创建空文件。
 * @return FS_OK 成功；FS_ERR_EXIST 已存在；FS_ERR_NOSPC 文件槽满
 */
fs_err_t fs_create(const flash_hal_t *hal, const char *name);

/**
 * 删除文件（数据块释放为孤儿，后续分配自动复用）。
 */
fs_err_t fs_delete(const flash_hal_t *hal, const char *name);

/**
 * 覆盖写：文件不存在则创建。
 * 新数据长度不超过已分配块时原地擦写复用；否则迁移到新空闲块（两步
 * 提交更新 FAT）。
 * @param len 写入字节数（0 表示清空文件内容）
 */
fs_err_t fs_write(const flash_hal_t *hal, const char *name,
                  const void *buf, uint32_t len);

/**
 * 追加写：优先使用文件末尾空闲字节，空间不足时自动扩展。
 */
fs_err_t fs_append(const flash_hal_t *hal, const char *name,
                   const void *buf, uint32_t len);

/**
 * 读取文件数据。
 * @param offset 相对文件头偏移；len 超出文件尾时只读有效部分
 * @param len    in: 请求长度；out: 实际读取字节数
 */
fs_err_t fs_read(const flash_hal_t *hal, const char *name,
                 void *buf, uint32_t offset, uint32_t *len);

/**
 * 查询文件大小（字节）。
 */
fs_err_t fs_get_size(const flash_hal_t *hal, const char *name, uint32_t *size);

/**
 * 查询文件是否存在。
 */
bool fs_exists(const flash_hal_t *hal, const char *name);

/**
 * 统计当前文件数。
 */
uint32_t fs_file_count(const flash_hal_t *hal);

/**
 * 返回 FS 内部块大小（= 底层 erase_size）。
 */
uint32_t fs_block_size(const flash_hal_t *hal);

#ifdef __cplusplus
}
#endif

#endif /* FS_STORE_H */
