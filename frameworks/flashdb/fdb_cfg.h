/**
 * fdb_cfg.h - FlashDB 裁剪配置（对接本平台模拟基座）
 *
 * 说明：替代上游 inc/fdb_cfg_template.h。本平台把 FlashDB 作为 KV 组件使用，
 * 因此仅开启 KVDB，关闭 TSDB（时序库），并采用 FAL 存储模式，
 * 通过 fal_flash_sim_port.c 把 FAL 的 read/write/erase 接到 flash_sim。
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* 使用 KVDB（键值数据库）功能 */
#define FDB_USING_KVDB

#ifdef FDB_USING_KVDB
/* 默认 KV 版本号变化时自动更新（本平台不启用，避免测试间互相干扰） */
/* #define FDB_KV_AUTO_UPDATE */
#endif

/* 不使用 TSDB（本平台仅将 FlashDB 作为 KV 组件） */
/* #define FDB_USING_TSDB */

/* 使用 FAL 存储模式：由 FAL 抽象层对接模拟基座 */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/*
 * 写粒度（单位 bit）。模拟基座支持字节级写入且遵循 NOR "仅 1->0" 语义，
 * 故取 1（nor flash）。
 */
#define FDB_WRITE_GRAN                 1
#endif

/* 小端（与 x86/多数 ARM 一致），保持默认 */
/* #define FDB_BIG_ENDIAN */

/*
 * 日志输出：默认走 printf。本平台把 FlashDB 内部调试日志交由移植层控制，
 * 通过 FDB_PRINT 重定向到可开关的包装函数（见 fal_flash_sim_port.c）。
 */
#define FDB_PRINT(...)                 fdb_sim_print(__VA_ARGS__)
extern void fdb_sim_print(const char *fmt, ...);

/* 打开调试信息（实际是否输出由 fdb_sim_print 内的开关决定） */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
