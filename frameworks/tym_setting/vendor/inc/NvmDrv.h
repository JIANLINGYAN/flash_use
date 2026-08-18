/**
 * @file        NvmDrv.h
 * @brief       NVM 存储驱动（对接 Flash）
 *
 * 去耦裁剪：删除 #include "hal_flash.h"、#include "attachedDevices.h"、
 * #include "ProductDefine.h"。Flash 访问与设备获取由移植层实现
 * （tym_setting_sim_port.c）。
 */

#ifndef NVMDRV_H
#define NVMDRV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cplus.h"
#include "StorageDrv.h"

SUBCLASS(cNvmDrv, cStorageDrv)
    /* private data */
    uint32 currAddr;
METHODS
    /* public functions */
void NvmDrv_Ctor(cNvmDrv *me);
void NvmDrv_Xtor(cNvmDrv *me);

END_CLASS

/**
 * 擦除全部 NVM 存储分区。
 * @return TRUE 成功 / FALSE 失败
 */
BOOL NvmDrv_EraseAll(cNvmDrv *me);

/** 单字写（bootloader 兼容保留） */
bool NvmDrv_WriteWord(uint32 addr, uint32 wData);

/** 单字读（bootloader 兼容保留） */
bool NvmDrv_ReadWord(uint32 addr, uint32 *pReadData);

#ifdef __cplusplus
}
#endif

#endif /* NVMDRV_H */
