/**
 * tym_setting_sim_port.c - TYM Setting 组件移植层：对接模拟基座 flash_sim
 *
 * 定位：实现 NvmDrv_Ctor 的三个 Flash 回调（SetValue/GetValue/ErasePage）
 * 桥接 flash_sim。TYM Setting 框架的地址是"分区绝对偏移"（基址 0），
 * 故可直接透传 flash_sim。
 *
 * 已知原始缺陷的修复（见 PORTING.md）：
 *   - NvmDrv_IsError 原恒返回 true（错误被吞），本实现返回真实结果；
 *   - 4 字节对齐校验恢复（配合 config 的 SETT_ELEMENT_MIN_SIZE）。
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "NvmDrv.h"
#include "NvmDrv_priv.h"
#include "StorageDrv.h"

#include "flash_sim.h"
#include "tym_setting_sim_port.h"
#include "tym_setting_log.h"

/* ==================== 注入状态 ==================== */

static flash_dev_t *s_dev = NULL;
static uint32_t s_base_addr = 0;
static uint32_t s_capacity = 0;
static uint32_t s_erase_size = 4096;

void tym_setting_sim_setup(flash_dev_t *dev, uint32_t base_addr,
                           uint32_t capacity, uint32_t erase_size)
{
    s_dev = dev;
    s_base_addr = base_addr;
    s_capacity = capacity;
    s_erase_size = erase_size;
}

flash_dev_t *tym_setting_sim_device(void)
{
    return s_dev;
}

int tym_setting_sim_erase_all(void)
{
    if (s_dev == NULL || s_capacity == 0) {
        return -1;
    }
    if (flash_sim_erase(s_dev, s_base_addr, s_capacity) != FLASH_OK) {
        return -1;
    }
    return 0;
}

/* ==================== NVM 驱动实现 ==================== */

void NvmDrv_Ctor(cNvmDrv *me)
{
    if (me == NULL) {
        return;
    }
    me->super_.SetValue = NvmDrv_WriteData;
    me->super_.GetValue = NvmDrv_ReadData;
    me->super_.ErasePage = NvmDrv_ErasePage;
    me->currAddr = 0;
}

void NvmDrv_Xtor(cNvmDrv *me)
{
    (void)me;
}

/* Flash 写：要求 4 字节对齐；写入前目标须为 0xFF（调用方保证） */
static bool NvmDrv_WriteData(cStorageDrv *me, uint32 addr, uint8 *pBuf,
                             uint32 sizeInBytes)
{
    (void)me;
    if (s_dev == NULL || pBuf == NULL || sizeInBytes == 0) {
        return FALSE;
    }
    if (sizeInBytes % 4u != 0u) {
        LOG_E("NvmDrv_WriteData: 长度 %lu 非 4 字节对齐",
              (unsigned long)sizeInBytes);
        return FALSE;
    }
    return flash_sim_write(s_dev, s_base_addr + addr, pBuf, sizeInBytes)
               == FLASH_OK ? TRUE : FALSE;
}

static bool NvmDrv_ReadData(cStorageDrv *me, uint32 addr, uint8 *pBuf,
                            uint32 sizeInBytes)
{
    (void)me;
    if (s_dev == NULL || pBuf == NULL || sizeInBytes == 0) {
        return FALSE;
    }
    return flash_sim_read(s_dev, s_base_addr + addr, pBuf, sizeInBytes)
               == FLASH_OK ? TRUE : FALSE;
}

/* 擦除 addr 所在整个 Setting 分区（与 config 的擦除粒度一致） */
static bool NvmDrv_ErasePage(cStorageDrv *me, uint32 addr)
{
    uint32_t region_start = s_base_addr;
    uint32_t region_size = s_capacity;

    (void)me;
    if (s_dev == NULL || region_size == 0) {
        return FALSE;
    }
    /* 按实际擦除块粒度逐块擦（模拟基座按块擦除） */
    if (addr < region_start || addr >= region_start + region_size) {
        LOG_E("NvmDrv_ErasePage: addr=0x%lx 越界", (unsigned long)addr);
        return FALSE;
    }
    if (flash_sim_erase(s_dev, region_start, region_size) != FLASH_OK) {
        return FALSE;
    }
    return TRUE;
}

bool NvmDrv_WriteWord(uint32 addr, uint32 wData)
{
    if (s_dev == NULL) {
        return FALSE;
    }
    return flash_sim_write(s_dev, s_base_addr + addr, &wData, sizeof(wData))
               == FLASH_OK ? TRUE : FALSE;
}

bool NvmDrv_ReadWord(uint32 addr, uint32 *pReadData)
{
    if (s_dev == NULL || pReadData == NULL) {
        return FALSE;
    }
    return flash_sim_read(s_dev, s_base_addr + addr, pReadData,
                          sizeof(*pReadData)) == FLASH_OK ? TRUE : FALSE;
}

BOOL NvmDrv_EraseAll(cNvmDrv *me)
{
    (void)me;
    return tym_setting_sim_erase_all() == 0 ? TRUE : FALSE;
}
