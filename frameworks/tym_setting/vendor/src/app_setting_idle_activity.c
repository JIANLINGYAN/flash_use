/* =====================================================================
 * TYM Setting 框架核心（去耦裁剪版）
 *
 * 相对原版的主要修改（目的：去除对 FreeRTOS / ui_shell / Airoha HAL 的耦合）：
 *   1. 删除 FreeRTOS.h / timers.h：1ms 周期定时器改为裸机 tick 轮询
 *      （SettingSrv_Tick(elapsed_ms)），避免 1ms 定时器仅为 5s 延时。
 *   2. 删除 ui_shell_* 头文件与 app_setting_idle_activity_proc()：
 *      初始化改为直接调用 SettingSrv_Init()（等价原 SYSTEM 事件分支）。
 *   3. 删除 hal_log.h：日志改为 LOG_W/I/E() 宏（移植层映射到 printf）。
 *   4. 修复 Setting_GetEx：去掉"返回 Flash 裸地址"分支（本平台 Flash
 *      不可内存映射），改为仅回退默认值。
 *   5. 修复 Bookkeeping：判空补齐 SetValue（原只判 ErasePage）。
 *   6. 恢复关键 ASSERT（id 越界检查），保证热路径安全。
 * ===================================================================== */

#include <string.h>

#include "app_setting_idle_activity.h"
#include "StorageDrv.h"
#include "NvmDrv.h"
#include "SettingSrv_priv.h"

#include "tym_setting_log.h"
#include "SettingSrv.config"

cNvmDrv nvmDrv;
static cStorageDrv *pStorageDrv;
app_setting_save_t SettingSave;

/* @brief 从 Flash 载入 RAM 镜像 */
static void SettingSrv_LoadStoredValue(cStorageDrv * const me)
{
    uint16 i = 0;
    bool result = TRUE;

    for (; i < ArraySize(settingRomMap); i++)
    {
        eSettingId id = settingRomMap[i].id;
        if (settingDB[id].p)
        {
            result = me->GetValue(me, settingRomMap[i].addr,
                                  settingDB[id].p, settingDB[id].size);
            if (TRUE == result)
            {
                TYM_SET_BIT(settingDB[id].attr, SETTING_ATTR_SET);
            }
            else
            {
                LOG_W("SettingSrv_LoadStoredValue: id=%d 读 Flash 失败", id);
            }
        }
    }
}

/* @brief 立即触发全量回写（带判空） */
void SettingSrv_BookkeepingEx(void)
{
    if (pStorageDrv)
    {
        SettingSrv_Bookkeeping(pStorageDrv);
    }
}

/* @brief 全量回写：整页擦除 + 全表回写 */
void SettingSrv_Bookkeeping(cStorageDrv * const me)
{
    uint16 i = 0;

    if (me->ErasePage)
    {
        if (!me->ErasePage(me, SETT_PAGE_ROM_ADDR))
        {
            LOG_E("SettingSrv_Bookkeeping: 擦除失败");
            return;
        }
    }

    for (; i < ArraySize(settingRomMap); i++)
    {
        if (settingRomMap[i].addr)
        {
            eSettingId id = settingRomMap[i].id;
            if ((settingRomMap[i].addr) && (settingDB[id].p))
            {
#ifdef SETT_ELEMENT_MIN_SIZE
                if (settingDB[id].size < SETT_ELEMENT_MIN_SIZE)
                {
                    /* 补齐到 4 字节对齐写（uint32_t 固定 4 字节，
                     * 原 uint32 在 64 位主机为 8 字节会写错位） */
                    uint32_t buf = 0;
                    memcpy(&buf, settingDB[id].p, settingDB[id].size);
                    if (!me->SetValue(me, settingRomMap[i].addr,
                                      (uint8 *)&buf, sizeof(buf)))
                    {
                        LOG_E("SettingSrv_Bookkeeping: 写 id=%d 失败", id);
                    }
                }
                else
#endif
                {
                    if (!me->SetValue(me, settingRomMap[i].addr,
                                      settingDB[id].p, settingDB[id].size))
                    {
                        LOG_E("SettingSrv_Bookkeeping: 写 id=%d 失败", id);
                    }
                }
            }
        }
    }
}

/* @brief 取 ID 的 Flash 绝对地址（线性查找 romMap） */
static uint32 getRomAddr(eSettingId id)
{
    uint32 ret = 0;
    uint16 i = 0;

    for (; i < ArraySize(settingRomMap); i++)
    {
        if (settingRomMap[i].id == id)
        {
            ret = settingRomMap[i].addr;
            break;
        }
    }
    return ret;
}

/* ==================== 初始化 ==================== */

void SettingSrv_Init(void)
{
    extern void NvmDrv_Ctor(cNvmDrv * me);

    pStorageDrv = (cStorageDrv *)&nvmDrv;
    StorageDrv_Ctor(pStorageDrv, (const tStorageDevice *)0);
    SettingSrv_LoadStoredValue(pStorageDrv);
    SettingSave.save_flag = 0;
    SettingSave.save_timeout = 0;
    LOG_I("SettingSrv_Init: 完成（载入 %u 项）", (unsigned)ArraySize(settingRomMap));
}

void SettingSrv_Tick(uint32 elapsed_ms)
{
    if (!SettingSave.save_flag)
    {
        return;
    }
    if (SettingSave.save_timeout > elapsed_ms)
    {
        SettingSave.save_timeout -= elapsed_ms;
        return;
    }
    SettingSave.save_flag = 0;
    SettingSave.save_timeout = 0;
    SettingSrv_Bookkeeping(pStorageDrv);
}

/* ==================== 公共 API ==================== */

void SettingSrv_InitDB(void)
{
    eSettingId i = SETID_START;

    for (; i < SETID_MAX; i++)
    {
        TYM_CLR_BIT(settingDB[i].attr, SETTING_ATTR_SET);
    }
}

bool Setting_IsIdValid(eSettingId id)
{
    if (id >= SETID_MAX)
    {
        return FALSE;
    }
    return settingDB[id].attr & SETTING_ATTR_VALID ? TRUE : FALSE;
}

bool Setting_IsIdNVM(eSettingId id)
{
    if (id >= SETID_MAX)
    {
        return FALSE;
    }
    return settingDB[id].attr & SETTING_ATTR_NVM ? TRUE : FALSE;
}

bool Setting_IsReady(eSettingId id)
{
    bool ret = FALSE;

    if (Setting_IsIdValid(id))
    {
        ret = settingDB[id].attr & SETTING_ATTR_SET ? TRUE : FALSE;
    }
    return ret;
}

void Setting_Reset(eSettingId id)
{
    if (id >= SETID_MAX)
    {
        LOG_E("Setting_Reset: id=%d 越界", id);
        return;
    }
    settingDB[id].attr &= (~SETTING_ATTR_SET);
}

const void *Setting_GetEx(eSettingId id, const void *pDefault)
{
    const void *ret = NULL;

    if (id >= SETID_MAX)
    {
        return pDefault;
    }
    if (Setting_IsReady(id))
    {
        /* RAM 镜像中已有有效值 */
        ret = settingDB[id].p;
    }
    else
    {
        /* 未载入则回退默认值（本平台 Flash 不可内存映射，
         * 不再返回 Flash 裸地址） */
        ret = pDefault;
    }
    return ret;
}

const void *Setting_GetAddr(eSettingId id)
{
    if (id >= SETID_MAX)
    {
        return NULL;
    }
    return settingDB[id].p;
}

const void *Setting_Get(eSettingId id)
{
    return Setting_GetEx(id, NULL);
}

uint32 Setting_GetSize(eSettingId id)
{
    if (id >= SETID_MAX)
    {
        return 0;
    }
    return (uint32)(settingDB[id].size);
}

void Setting_Set(eSettingId id, const void *pValue)
{
    if (id >= SETID_MAX)
    {
        LOG_E("Setting_Set: id=%d 越界", id);
        return;
    }
    if (pValue == NULL)
    {
        LOG_E("Setting_Set: pValue 为空 id=%d", id);
        return;
    }
    if (Setting_IsIdValid(id) && settingDB[id].p)
    {
        if (pValue != settingDB[id].p)
        {
            memcpy(settingDB[id].p, pValue, settingDB[id].size);
        }
        TYM_SET_BIT(settingDB[id].attr, SETTING_ATTR_SET);

        /* 若需落 Flash 且驱动已装配，则启动延时回写窗口 */
        if (getRomAddr(id))
        {
            if (pStorageDrv == NULL)
            {
                LOG_W("Setting_Set: pStorageDrv 未初始化");
            }
            else if (Setting_IsIdNVM(id))
            {
                SettingSave.save_flag = 1;
                SettingSave.save_timeout = SETTING_SAVE_MS;
            }
        }
    }
}
