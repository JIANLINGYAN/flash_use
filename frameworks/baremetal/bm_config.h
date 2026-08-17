/**
 * bm_config.h - 裸机结构体配置存储框架（A/B 双备份 + CRC 校验）
 *
 * 设计定位（对应设计文档模块 2.3「裸机结构体框架」，静态小数据场景）：
 *  - 把一个应用配置结构体整块映射到 Flash 固定地址，读写以"整块"为单位，
 *    无索引、无 GC、代码量极小（适合无 RTOS 的裸机 MCU）。
 *  - A/B 双分区轮换：每次保存写入"当前较旧"的那个分区，写成功后它成为最新。
 *    任一分区损坏（掉电写坏/CRC 错）时可从另一分区恢复，保证永不丢配置。
 *  - CRC32 整块校验 + 单调递增序号(seq)：用 seq 判断哪个分区更新，
 *    用 CRC 判断分区是否完好，二者结合实现掉电安全。
 *
 * 掉电安全原理：
 *    写入分区 X 的过程中掉电 -> X 的 CRC 必然不匹配（数据不完整）
 *                            -> 启动时选择另一分区 Y（CRC 正确、seq 次新）
 *    因此任何时刻至多损失"最后一次未完成的保存"，绝不会两个分区同时损坏。
 *
 * 平台无关：仅依赖 C99 与 flash_sim 接口；移植到真实 MCU 只需替换
 * flash_sim_read/write/erase 为对应驱动。
 */

#ifndef BM_CONFIG_H
#define BM_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "flash_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 记录头 Magic："BMCF" */
#define BM_MAGIC          0x424D4346u

/* 支持的最大配置体大小（字节），超出则 bm_config_init 返回错误 */
#define BM_MAX_PAYLOAD    1024u

/**
 * 分区头部（位于每个备份分区起始处，紧随其后是配置体数据）。
 * 布局：[bm_header_t][payload ... len 字节]
 */
typedef struct {
    uint32_t magic;   /* BM_MAGIC，用于识别已写入的合法分区 */
    uint32_t seq;     /* 单调递增序号，大者为最新 */
    uint32_t len;     /* payload 长度（字节） */
    uint32_t crc;     /* payload 的 CRC32 */
} bm_header_t;

/**
 * 框架句柄。使用者需为其分配存储（可为全局静态变量，裸机无动态内存）。
 */
typedef struct {
    flash_dev_t *dev;      /* 底层 Flash 设备 */
    uint32_t base_a;       /* 分区 A 起始偏移（块对齐） */
    uint32_t base_b;       /* 分区 B 起始偏移（块对齐） */
    uint32_t part_size;    /* 单个分区大小（字节，>= 头部+payload） */
    uint32_t payload_len;  /* 配置体长度（字节） */
    uint32_t cur_seq;      /* 当前生效的序号 */
    int8_t   cur_part;     /* 当前生效分区：0=A，1=B，-1=两者均无效 */
} bm_config_t;

/**
 * 初始化：绑定设备与 A/B 分区，并扫描两个分区确定当前生效者。
 *
 * @param ctx         句柄（由调用者提供存储）
 * @param dev         已由 flash_sim_init 创建的设备
 * @param base_a      分区 A 起始偏移（须块对齐）
 * @param base_b      分区 B 起始偏移（须块对齐，且不与 A 重叠）
 * @param part_size   单分区大小（须 >= sizeof(bm_header_t)+payload_len）
 * @param payload_len 配置体长度（<= BM_MAX_PAYLOAD）
 * @return            FLASH_OK 成功（无论是否已有有效数据）；
 *                    FLASH_ERR_ARGS 参数非法
 */
flash_err_t bm_config_init(bm_config_t *ctx, flash_dev_t *dev,
                           uint32_t base_a, uint32_t base_b,
                           uint32_t part_size, uint32_t payload_len);

/**
 * 读取当前生效的配置。
 *
 * @param out  输出缓冲（至少 payload_len 字节）
 * @return     FLASH_OK 读到有效配置；
 *             FLASH_ERR_ARGS 两个分区均无有效数据（首次上电，应写入默认值）
 */
flash_err_t bm_config_load(bm_config_t *ctx, void *out);

/**
 * 保存配置（写入较旧的那个分区，实现 A/B 轮换与磨损分摊）。
 * 写入流程：擦除目标分区 -> 写 payload -> 写头部（含 CRC 与新 seq）。
 * 头部最后写入，确保中途掉电时该分区不会被误判为有效。
 *
 * @param in  配置体数据（payload_len 字节）
 * @return    FLASH_OK 成功；其他为介质错误
 */
flash_err_t bm_config_save(bm_config_t *ctx, const void *in);

/**
 * 恢复出厂：擦除两个分区（下次 load 将返回 FLASH_ERR_ARGS）。
 */
flash_err_t bm_config_reset(bm_config_t *ctx);

/**
 * 分区健康状态（用于诊断与前端展示）。
 */
typedef struct {
    bool     a_valid;   /* 分区 A 的 magic+CRC 是否有效 */
    bool     b_valid;   /* 分区 B 的 magic+CRC 是否有效 */
    uint32_t a_seq;     /* 分区 A 的序号（无效时为 0） */
    uint32_t b_seq;     /* 分区 B 的序号（无效时为 0） */
    int8_t   active;    /* 当前生效分区：0=A，1=B，-1=无 */
} bm_status_t;

flash_err_t bm_config_status(bm_config_t *ctx, bm_status_t *st);

/**
 * 计算 CRC32（对外暴露，便于上层自行校验或做单元测试）。
 */
uint32_t bm_crc32(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BM_CONFIG_H */
