/**
 * ef_cfg.h - EasyFlash 裁剪配置（对接本平台模拟基座）
 *
 * 说明：本文件替代上游 easyflash/inc/ef_cfg.h，仅开启 ENV(KV) 功能，
 * 关闭 IAP / LOG，使 EasyFlash 以纯 KV 组件形式参与本平台仿真。
 *
 * 关键取值与模拟基座的对应关系：
 *   EF_ERASE_MIN_SIZE  <- flash_config_t.erase_size（擦除块大小）
 *   EF_START_ADDR      <- KV 区域在介质中的起始偏移
 *   ENV_AREA_SIZE      <- KV 区域大小（须 >= 2 个擦除块，GC 需要空闲块）
 * 三者在运行期由 ef_port.c 通过 ef_port_setup() 注入，编译期用宏转发，
 * 从而实现"同一份库、参数可配置"（前端可调整介质与 KV 区参数）。
 */

#ifndef EF_CFG_H_
#define EF_CFG_H_

#include <stdint.h>
#include <stddef.h>

/* ---- 运行期参数（由 ef_port_setup 注入，供下方宏引用） ---- */
extern uint32_t ef_cfg_erase_min_size(void);
extern uint32_t ef_cfg_start_addr(void);
extern uint32_t ef_cfg_env_area_size(void);

/*
 * 注意：因上述三项改为运行期取值，上游 ef_env.c 中依赖它们的两条编译期
 * #error 检查已下移为运行期 EF_ASSERT（见 vendor/src/ef_env.c 中标注
 * "[flash_use 平台适配]" 处）。约束语义不变：
 *   - ENV 区须按擦除块对齐且至少 2 个块
 *   - GC 至少保留一个空块
 */

/* 使用 ENV(KV) 功能，NG 模式（V4.0+ 默认，自带磨损均衡与掉电保护） */
#define EF_USING_ENV

#ifdef EF_USING_ENV
/* ENV 版本号：默认 ENV 集合变化时递增 */
#define EF_ENV_VER_NUM            0
#endif /* EF_USING_ENV */

/* 不使用 IAP / LOG（本平台仅将 EasyFlash 作为 KV 组件） */
/* #define EF_USING_IAP */
/* #define EF_USING_LOG */

/*
 * 最小擦除单位。转发到运行期配置，使前端可调节擦除块大小。
 * 注意：EasyFlash 内部会用它做对齐断言与块遍历。
 */
#define EF_ERASE_MIN_SIZE         ef_cfg_erase_min_size()

/*
 * 写粒度（单位 bit）。模拟基座支持字节级写入（write_size=1），
 * 且 NOR 语义为"仅能 1->0"，与 EF_WRITE_GRAN=1 完全吻合。
 */
#define EF_WRITE_GRAN             1

/* read_env / continue_ff_addr 使用的缓冲大小 */
#define EF_READ_BUF_SIZE          32

/* 备份区起始地址与 ENV 区大小：转发到运行期配置 */
#define EF_START_ADDR             ef_cfg_start_addr()
#define ENV_AREA_SIZE             ef_cfg_env_area_size()

/* 关闭上游冗长的 debug 打印（本平台由测试程序统一输出） */
/* #define PRINT_DEBUG */

#endif /* EF_CFG_H_ */
