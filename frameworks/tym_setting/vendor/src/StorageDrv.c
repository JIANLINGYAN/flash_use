/* =====================================================================
 * TYM Setting 存储驱动工厂（去耦裁剪版）
 *
 * 相对原版的主要修改：
 *   1. 删除 deviceTypes.h / attachedDevices.h 依赖；
 *   2. 原实现按 pStorageConfig->deviceInfo.deviceType 做 switch 分派，
 *      本平台仅有一种 NVM 驱动，改为直接装配 NvmDrv_Ctor()。
 *      若传入 NULL 配置，则直接装配默认 NVM 驱动（单驱动平台）。
 * ===================================================================== */

#include "StorageDrv_priv.h"
#include "NvmDrv.h"

void StorageDrv_Ctor(cStorageDrv *me, const tStorageDevice *pStorageConfig)
{
    cNvmDrv *pNvmObj;

    if (me == NULL)
    {
        return;
    }
    pNvmObj = (cNvmDrv *)me;
    /* NVM 驱动实现不再读取 pStorageConfig（见 tym_setting_sim_port.c），
     * 仅保留透传以便兼容原框架语义 */
    me->pStorageConfig = pStorageConfig;
    NvmDrv_Ctor(pNvmObj);
}

void StorageDrv_Xtor(cStorageDrv *me)
{
    (void)me;
}
