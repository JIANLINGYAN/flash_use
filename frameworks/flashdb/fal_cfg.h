/**
 * fal_cfg.h - FAL（Flash 抽象层）配置：设备表与分区表
 *
 * FAL 要求以编译期宏形式提供"Flash 设备表"与"分区表"。本平台只挂载一个
 * 模拟设备 flash_sim_dev（对应 flash_sim 的 BIN 介质），并在其上划出一个
 * KVDB 分区 "fdb_kvdb1"。
 *
 * 关于"参数可配置"：设备的 addr/len/blk_size 与分区的 offset/len 原本是
 * 编译期常量，本平台需要让前端可调整介质与 KV 区大小，实现方式为：
 *   - 设备表项 flash_sim_dev 是非 const 全局变量，其 addr/len/blk_size 在
 *     fal_init() 之前由 fal_sim_port_setup() 于运行期填充；
 *   - 分区表宏中的 offset/len 仅作为"占位默认值"（编译期常量，满足 FAL 的
 *     static const 初始化要求）；fal_init() 之后，移植层再调用上游提供的
 *     官方接口 fal_set_partition_table_temp() 用运行期计算出的真实
 *     offset/len 替换分区表。该接口正是为运行期动态分区场景设计的。
 * 调用顺序：fal_sim_port_setup() -> fal_init() -> 安装运行期分区表。
 * 上述流程全部封装在 fal_sim_port_init() 中，测试程序只需调用它。
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

/* 关闭 FAL 冗长调试日志（本平台由测试程序统一输出） */
#define FAL_DEBUG                 0

/*
 * FAL 默认用 printf 直接输出带 ANSI 颜色码的日志，会污染前端逐行解析的
 * 结果面板。此处重定向到移植层的可开关打印函数（与 FDB_PRINT 同一开关），
 * 仅在 FDB_VERBOSE 非 0 时输出。
 */
#define FAL_PRINTF(...)           fdb_sim_print(__VA_ARGS__)
extern void fdb_sim_print(const char *fmt, ...);

/* 使用编译期分区表（而非从 Flash 读取分区表） */
#define FAL_PART_HAS_TABLE_CFG

/* KVDB 分区名（供移植层与测试程序共用） */
#define FAL_KVDB_PART_NAME        "fdb_kvdb1"

/*
 * 分区表魔数。上游把它定义在 fal_partition.c 内部（不对外导出），
 * 而分区表宏与运行期分区表都需要它，故在此给出同值定义。
 * 取值必须与 fal_partition.c 中的 FAL_PART_MAGIC_WORD 保持一致。
 */
#ifndef FAL_PART_MAGIC_WORD
#define FAL_PART_MAGIC_WORD       0x45503130
#endif

/* ===================== Flash 设备配置 ========================= */
extern struct fal_flash_dev flash_sim_dev;

#define FAL_FLASH_DEV_TABLE       \
{                                 \
    &flash_sim_dev,               \
}

/* ====================== 分区配置 ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG
/*
 * 分区表：offset/len 为占位值，运行期由 fal_sim_port_setup() 覆写。
 * 字段依次为：magic、分区名、所属 flash 设备名、偏移、长度、保留。
 */
#define FAL_PART_TABLE                                                       \
{                                                                            \
    {FAL_PART_MAGIC_WORD, FAL_KVDB_PART_NAME, "flash_sim", 0, 16 * 1024, 0}, \
}
#endif /* FAL_PART_HAS_TABLE_CFG */

#endif /* _FAL_CFG_H_ */
