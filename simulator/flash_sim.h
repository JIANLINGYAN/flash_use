/**
 * flash_sim.h - 模拟基座抽象接口层
 *
 * 设计目标：平台无关、可仿真。所有存储框架（KV/NVS、文件系统等）
 * 通过统一的 Flash 操作接口访问底层存储介质，从而可在 PC 端用 BIN
 * 文件模拟真实 Flash 的物理特性（按页写、按块擦、寿命、坏点等）。
 *
 * 参考 Zephyr flash_simulator 的接口思想。
 */

#ifndef FLASH_SIM_H
#define FLASH_SIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flash 介质类型 */
typedef enum {
    FLASH_TYPE_NOR     = 0, /* 按页写入、按块擦除，支持 XIP */
    FLASH_TYPE_NAND    = 1, /* 大容量、块擦除、坏块 */
    FLASH_TYPE_EEPROM  = 2  /* 字节级读写、超长寿命 */
} flash_type_t;

/*
 * 三类介质的默认物理指标（依据嵌入式 Flash 硬件特性表）。
 * 这些默认值供模拟基座与各存储框架初始化时引用，使"未显式配置"
 * 的场景也能体现真实硬件的读写粒度、擦除代价与寿命差异。
 *
 * 取值来源（典型值）：
 *  - NOR  : 读字节级(XIP) / 写页(256B) / 擦除扇区4KB / 扇区擦除几十ms /
 *           页编程几十~几百us / 寿命 10K~100K 次/扇区。
 *  - NAND : 读按页 / 写按页(2KB) / 擦除块(由多页组成) / 块擦除 1~数ms /
 *           页编程 200~800us / 寿命 SLC 10K~100K 次/块 / 存在坏块需 ECC。
 *  - EEPROM: 字节级读写 / 单字节原地改写、无需擦除 / 单字节写几百us /
 *           寿命 100K~1M 次/字节。
 */
#define FLASH_NOR_TOTAL        (64ul * 1024)   /* 默认容量 64KB（主流 SPI-NOR） */
#define FLASH_NOR_ERASE        4096u           /* 扇区擦除粒度 4KB */
#define FLASH_NOR_WRITE        1u              /* 模拟层按字节编程（硬件页=256B，但不强制整页） */
#define FLASH_NOR_READ         1u              /* 任意字节随机读 */
#define FLASH_NOR_CYCLES       100000u         /* 擦写寿命 10万次/扇区 */
#define FLASH_NOR_READ_US      50u             /* 字节读 ~50us（优秀，XIP） */
#define FLASH_NOR_WRITE_US     200u            /* 页编程 ~200us（按页计量，字节写按比例近似） */
#define FLASH_NOR_ERASE_US     40000u          /* 扇区擦除 ~40ms */

#define FLASH_NAND_TOTAL       (128ul * 1024 * 1024) /* 默认容量 128MB */
#define FLASH_NAND_ERASE       (128u * 1024)  /* 块擦除粒度 128KB（多页组成） */
#define FLASH_NAND_WRITE       1u              /* 模拟层按字节编程（硬件按页 2KB；模拟支持字节访问便于框架复用） */
#define FLASH_NAND_READ        1u              /* 模拟层按字节读（硬件按页） */
#define FLASH_NAND_CYCLES      100000u         /* SLC 擦写寿命 10万次/块 */
#define FLASH_NAND_READ_US     100u            /* 页读 ~100us */
#define FLASH_NAND_WRITE_US    500u            /* 页编程 ~500us（200~800us） */
#define FLASH_NAND_ERASE_US    3000u           /* 块擦除 ~3ms（1~数ms） */

#define FLASH_EEPROM_TOTAL     (32u * 1024)   /* 默认容量 32KB（外部 24Cxx 级） */
#define FLASH_EEPROM_ERASE     0u              /* 无擦除块概念 */
#define FLASH_EEPROM_WRITE     1u              /* 单字节可写 */
#define FLASH_EEPROM_READ      1u              /* 字节级随机读 */
#define FLASH_EEPROM_CYCLES    1000000u        /* 寿命 100万次/字节 */
#define FLASH_EEPROM_READ_US   50u             /* 字节读 ~50us */
#define FLASH_EEPROM_WRITE_US  500u            /* 单字节写 ~500us */
#define FLASH_EEPROM_ERASE_US  0u              /* 无擦除 */

/*
 * 根据介质类型填充未显式配置的默认值。
 * 调用方先置零 cfg，再调用本宏填充类型相关默认项；
 * 再覆盖需要显式指定的字段（如 bin_path / bad_blocks 等）。
 */
#define FLASH_CFG_DEFAULTS_BY_TYPE(cfg, type) \
    do { \
        switch (type) { \
        case FLASH_TYPE_NAND: \
            (cfg).total_size = (cfg).total_size ? (cfg).total_size : FLASH_NAND_TOTAL; \
            (cfg).erase_size = (cfg).erase_size ? (cfg).erase_size : FLASH_NAND_ERASE; \
            (cfg).write_size = (cfg).write_size ? (cfg).write_size : FLASH_NAND_WRITE; \
            (cfg).read_size  = (cfg).read_size  ? (cfg).read_size  : FLASH_NAND_READ;  \
            (cfg).erase_cycles = (cfg).erase_cycles ? (cfg).erase_cycles : FLASH_NAND_CYCLES; \
            (cfg).read_us  = (cfg).read_us  ? (cfg).read_us  : FLASH_NAND_READ_US;  \
            (cfg).write_us = (cfg).write_us ? (cfg).write_us : FLASH_NAND_WRITE_US; \
            (cfg).erase_us = (cfg).erase_us ? (cfg).erase_us : FLASH_NAND_ERASE_US; \
            break; \
        case FLASH_TYPE_EEPROM: \
            (cfg).total_size = (cfg).total_size ? (cfg).total_size : FLASH_EEPROM_TOTAL; \
            (cfg).erase_size = 0; \
            (cfg).write_size = (cfg).write_size ? (cfg).write_size : FLASH_EEPROM_WRITE; \
            (cfg).read_size  = (cfg).read_size  ? (cfg).read_size  : FLASH_EEPROM_READ;  \
            (cfg).erase_cycles = (cfg).erase_cycles ? (cfg).erase_cycles : FLASH_EEPROM_CYCLES; \
            (cfg).read_us  = (cfg).read_us  ? (cfg).read_us  : FLASH_EEPROM_READ_US;  \
            (cfg).write_us = (cfg).write_us ? (cfg).write_us : FLASH_EEPROM_WRITE_US; \
            (cfg).erase_us = 0; \
            break; \
        case FLASH_TYPE_NOR: \
        default: \
            (cfg).total_size = (cfg).total_size ? (cfg).total_size : FLASH_NOR_TOTAL; \
            (cfg).erase_size = (cfg).erase_size ? (cfg).erase_size : FLASH_NOR_ERASE; \
            (cfg).write_size = (cfg).write_size ? (cfg).write_size : FLASH_NOR_WRITE; \
            (cfg).read_size  = (cfg).read_size  ? (cfg).read_size  : FLASH_NOR_READ;  \
            (cfg).erase_cycles = (cfg).erase_cycles ? (cfg).erase_cycles : FLASH_NOR_CYCLES; \
            (cfg).read_us  = (cfg).read_us  ? (cfg).read_us  : FLASH_NOR_READ_US;  \
            (cfg).write_us = (cfg).write_us ? (cfg).write_us : FLASH_NOR_WRITE_US; \
            (cfg).erase_us = (cfg).erase_us ? (cfg).erase_us : FLASH_NOR_ERASE_US; \
            break; \
        } \
    } while (0)

/* 通用错误码（与多数嵌入式 Flash 驱动约定一致，0 表示成功） */
typedef enum {
    FLASH_OK          = 0,
    FLASH_ERR_ARGS    = -1, /* 参数非法 */
    FLASH_ERR_RANGE   = -2, /* 地址/长度越界 */
    FLASH_ERR_ERASE   = -3, /* 擦除失败（坏块/寿命耗尽） */
    FLASH_ERR_WRITE   = -4, /* 写入失败 */
    FLASH_ERR_NOTSUP  = -5, /* 操作不支持（如对 EEPROM 调用 erase） */
    FLASH_ERR_IO      = -6  /* 文件 IO 失败 */
} flash_err_t;

/*
 * Flash 设备句柄。内部实现细节对使用者不透明，
 * 使用者只持有指针，符合嵌入式驱动"句柄"惯例。
 */
typedef struct flash_dev flash_dev_t;

/* Flash 几何与特性参数（可配置） */
typedef struct {
    flash_type_t type;        /* 介质类型 */
    uint32_t total_size;      /* 总容量（字节） */
    uint32_t erase_size;      /* 擦除块大小（字节），EEPROM 可设为 0 */
    uint32_t write_size;      /* 最小写入单位（字节），NOR 典型 1/4/256 */
    uint32_t read_size;       /* 最小读取单位（字节），通常 1 */
    uint32_t erase_cycles;    /* 标称擦写寿命（次），如 100000 */
    const char *bin_path;     /* BIN 文件路径（模拟物理介质） */

    /* ---- 性能与异常模拟（可配置，模拟真实 Flash 指标） ---- */
    uint32_t read_us;         /* 读操作耗时（微秒/次），0 表示不模拟延时 */
    uint32_t write_us;        /* 写操作耗时（微秒/次），0 表示不模拟延时 */
    uint32_t erase_us;        /* 擦除操作耗时（微秒/次），0 表示不模拟延时 */
    uint32_t bad_blocks;      /* 固定坏块数量（初始化时随机标记为坏块） */
    uint32_t bad_ratio;       /* 运行时坏块比率(0~10000，万分之一精度)，擦除时按概率注入坏块 */
} flash_config_t;

/**
 * 初始化（打开/创建）一个 Flash 模拟设备。
 * @param cfg  配置参数（bin_path 指向的 BIN 文件会被创建或加载）
 * @return     成功返回设备句柄，失败返回 NULL
 */
flash_dev_t *flash_sim_init(const flash_config_t *cfg);

/**
 * 关闭设备，刷新并关闭 BIN 文件。
 */
void flash_sim_deinit(flash_dev_t *dev);

/**
 * 读取数据。
 * @param offset  起始偏移（字节）
 * @param buf     输出缓冲
 * @param len     读取长度（字节）
 */
flash_err_t flash_sim_read(const flash_dev_t *dev, uint32_t offset,
                           void *buf, uint32_t len);

/**
 * 写入数据。
 * 注意：NOR/NAND 写入前对应区域须为已擦除状态（全 0xFF）。
 * EEPROM 支持任意地址直接覆盖写。
 */
flash_err_t flash_sim_write(flash_dev_t *dev, uint32_t offset,
                            const void *buf, uint32_t len);

/**
 * 擦除（仅 NOR/NAND 支持）。按 erase_size 对齐的块擦除。
 * @param offset  块对齐的起始偏移
 * @param len     擦除长度（应为 erase_size 的整数倍）
 */
flash_err_t flash_sim_erase(flash_dev_t *dev, uint32_t offset, uint32_t len);

/* ============ 异常模拟与统计接口 ============ */

/**
 * 查询某地址所在块的已擦写次数（用于磨损均衡/寿命监控）。
 * @param offset  任意地址（自动定位到块）
 * @param cycles  输出已擦写次数
 */
flash_err_t flash_sim_get_erase_count(const flash_dev_t *dev, uint32_t offset,
                                      uint32_t *cycles);

/**
 * 获取统计信息（含性能耗时与磨损概况）。
 */
typedef struct {
    uint32_t total_reads;     /* 累计读操作次数 */
    uint32_t total_writes;    /* 累计写操作次数 */
    uint32_t total_erases;    /* 累计擦除块数 */
    uint32_t total_write_bytes;/* 累计写入字节数 */
    uint32_t max_erase_cycles;/* 全片最高擦写次数 */
    uint32_t avg_erase_cycles;/* 全片平均擦写次数（有效块） */
    uint64_t read_time_us;    /* 累计读耗时（微秒） */
    uint64_t write_time_us;   /* 累计写耗时（微秒） */
    uint64_t erase_time_us;   /* 累计擦除耗时（微秒） */
    uint32_t bad_block_count; /* 当前已知坏块数量 */
} flash_stats_t;

flash_err_t flash_sim_get_stats(const flash_dev_t *dev, flash_stats_t *stats);

/**
 * 获取整片磨损分布（每块擦写次数），用于绘图。
 * @param out       输出数组（长度需 >= 块数）
 * @param max_blocks 数组容量（块数）；返回实际块数
 * @return          实际块数；若 out 为 NULL 仅返回块数
 */
uint32_t flash_sim_get_wear_map(const flash_dev_t *dev, uint32_t *out,
                                uint32_t max_blocks);

/**
 * 返回介质总块数（total_size / erase_size）。
 */
uint32_t flash_sim_block_count(const flash_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_SIM_H */
