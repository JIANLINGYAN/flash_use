/**
 * @file        NvmDrv_priv.h
 * @brief       It's the driver to read/write Non-Volatile Memory (NVM) of Microchip
 * @author      Johnny Fan 
 * @date        2014-03-17
 * @copyright   Tymphany Ltd.
 */
#ifndef NVMDRV_PRIV_H
#define NVMDRV_PRIV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "NvmDrv.h"

static bool NvmDrv_WriteData(cStorageDrv *me, uint32 addr, uint8 * pBuf, uint32 sizeInBytes);
static bool NvmDrv_ReadData(cStorageDrv *me, uint32 addr, uint8 * pBuf, uint32 sizeInBytes);
static bool NvmDrv_ErasePage(cStorageDrv *me, uint32 addr);

#ifdef __cplusplus
}
#endif

#endif /* NVMDRV_PRIV_H */
