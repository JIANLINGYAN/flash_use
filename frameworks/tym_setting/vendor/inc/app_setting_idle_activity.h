#ifndef __APP_SETTING_ACTIVITY_H__
#define __APP_SETTING_ACTIVITY_H__

/* =====================================================================
 * TYM Setting 框架公共头（去耦裁剪版）
 *
 * 相对原版的主要修改（目的：去除对 Airoha ui_shell / QP server 的耦合）：
 *   1. 删除 #include "ui_shell_activity.h" 与 #include "server.h"；
 *   2. 删除 REQ_EVT/RESP_EVT/IND_EVT 事件族（QP 形态，本包未启用
 *      SETTING_HAS_ROM_DATA，属死代码）；
 *   3. 删除 SUBCLASS(cSettingSrv, cServer) 类定义（QP 服务器形态）；
 *   4. 删除 app_setting_idle_activity_proc()（ui_shell 活动入口），
 *      替换为直接初始化入口 SettingSrv_Init()。
 * ===================================================================== */

#include "StorageDrv.h"
#include "setting_id.h"

/* 保存窗口控制块（Setting_Set 置位，SettingSrv_Tick 递减） */
typedef struct
{
    uint8  save_flag;
    uint32 save_timeout;
} app_setting_save_t;

/* ID → Flash 绝对地址 静态映射表项 */
typedef struct tSettingRomMap
{
    eSettingId id;
    uint32     addr;
} tSettingRomMap;

/* ==================== 公共 API ==================== */

/**
 * @brief 初始化：装配存储驱动（NvmDrv）+ 从 Flash 载入 RAM 镜像。
 *        等价原 _proc_ui_shell_group() 的 EVENT_GROUP_UI_SHELL_SYSTEM 分支。
 *        必须在首次 Setting_Get/Set 之前调用。
 */
void SettingSrv_Init(void);

/**
 * @brief 周期驱动（裸机 tick 轮询，替代原 FreeRTOS 1ms 定时器）。
 *        在保存窗口倒计时到 0 时触发整页回写。
 * @param elapsed_ms 距上次调用的毫秒数
 */
void SettingSrv_Tick(uint32 elapsed_ms);

/** 清除全部 SETTING_ATTR_SET 位（标记 RAM 无有效值） */
void SettingSrv_InitDB(void);

/** 读取：等价 Setting_GetEx(id, NULL) */
const void *Setting_Get(eSettingId id);

/** 读取（三级回退：RAM → 默认值；本平台 Flash 不可内存映射，去掉裸地址分支） */
const void *Setting_GetEx(eSettingId id, const void *pDefault);

/** 仅返回 RAM 镜像地址（可写入），不回退 */
const void *Setting_GetAddr(eSettingId id);

/** 返回 settingDB[id].size */
uint32 Setting_GetSize(eSettingId id);

/** 写 RAM 镜像 + 置位 + 启动延时回写窗口 */
void Setting_Set(eSettingId id, const void *pValue);

/** 是否 VALID 且 SET */
bool Setting_IsReady(eSettingId id);

/** 是否 SETTING_ATTR_VALID */
bool Setting_IsIdValid(eSettingId id);

/** 是否 SETTING_ATTR_NVM（需落 Flash） */
bool Setting_IsIdNVM(eSettingId id);

/** 清 SETTING_ATTR_SET（只影响 RAM 标记，不擦 Flash） */
void Setting_Reset(eSettingId id);

/** 立即触发全量回写（带 pStorageDrv 判空） */
void SettingSrv_BookkeepingEx(void);

/** 全量回写实现（隐含契约） */
void SettingSrv_Bookkeeping(cStorageDrv *me);

#endif /* __APP_SETTING_ACTIVITY_H__ */
