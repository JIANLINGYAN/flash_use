/**
 * @file        StorageDrv.h
 * @brief       通用存储驱动抽象（cStorageDrv 函数指针表）
 *
 * 去耦裁剪：删除 #include "ProductDefine.h" 与 #include "deviceTypes.h"。
 * tStorageDevice 类型由移植层提供（见 tym_setting_sim_port.h）。
 */

#ifndef STORAGEDRV_H
#define STORAGEDRV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cplus.h"
#include "commonTypes.h"

/* 存储设备配置（移植层定义，见 tym_setting_sim_port.h） */
typedef struct tStorageDevice tStorageDevice;

CLASS(cStorageDrv)
    /* private data */
    const tStorageDevice *pStorageConfig;
    /* 写值 */
    bool(*SetValue)(cStorageDrv *me, uint32 addr, uint8 *pBuf, uint32 sizeInBytes);
    /* 读值 */
    bool(*GetValue)(cStorageDrv *me, uint32 addr, uint8 *pBuf, uint32 sizeInBytes);
    /* 擦除一页 */
    bool(*ErasePage)(cStorageDrv *me, uint32 addr);
METHODS
    /* public functions */
void StorageDrv_Ctor(cStorageDrv *me, const tStorageDevice *pStorageConfig);
void StorageDrv_Xtor(cStorageDrv *me);
END_CLASS

#ifdef __cplusplus
}
#endif

#endif /* STORAGEDRV_H */
