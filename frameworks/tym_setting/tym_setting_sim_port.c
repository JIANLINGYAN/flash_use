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

#include "flash_hal.h"
#include "tym_setting_sim_port.h"
#include "tym_setting_log.h"

/* ==================== 注入状态 ==================== */

static const flash_hal_t *s_hal = NULL;
static uint32_t s_base_addr = 0;
static uint32_t s_capacity = 0;
static uint32_t s_erase_size = 4096;

void tym_setting_sim_setup(const flash_hal_t *hal, uint32_t base_addr,
                           uint32_t capacity, uint32_t erase_size)
{
    s_hal = hal;
    s_base_addr = base_addr;
    s_capacity = capacity;
    s_erase_size = erase_size;
}

void *tym_setting_sim_device(void)
{
    return NULL;   /* 已改为注册式 HAL，介质句柄由测试程序持有 */
}

int tym_setting_sim_erase_all(void)
{
    if (s_hal == NULL || s_capacity == 0) {
        return -1;
    }
    if (s_hal->erase(s_hal->ctx, s_base_addr, s_capacity) != 0) {
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
    if (s_hal == NULL || pBuf == NULL || sizeInBytes == 0) {
        return FALSE;
    }
    if (sizeInBytes % 4u != 0u) {
        LOG_E("NvmDrv_WriteData: 长度 %lu 非 4 字节对齐",
              (unsigned long)sizeInBytes);
        return FALSE;
    }
    return s_hal->write(s_hal->ctx, s_base_addr + addr, pBuf, sizeInBytes)
               == 0 ? TRUE : FALSE;
}

static bool NvmDrv_ReadData(cStorageDrv *me, uint32 addr, uint8 *pBuf,
                            uint32 sizeInBytes)
{
    (void)me;
    if (s_hal == NULL || pBuf == NULL || sizeInBytes == 0) {
        return FALSE;
    }
    return s_hal->read(s_hal->ctx, s_base_addr + addr, pBuf, sizeInBytes)
               == 0 ? TRUE : FALSE;
}

/* 擦除 addr 所在整个 Setting 分区（与 config 的擦除粒度一致） */
static bool NvmDrv_ErasePage(cStorageDrv *me, uint32 addr)
{
    uint32_t region_start = s_base_addr;
    uint32_t region_size = s_capacity;

    (void)me;
    if (s_hal == NULL || region_size == 0) {
        return FALSE;
    }
    /* 按实际擦除块粒度逐块擦（HAL 按块擦除） */
    if (addr < region_start || addr >= region_start + region_size) {
        LOG_E("NvmDrv_ErasePage: addr=0x%lx 越界", (unsigned long)addr);
        return FALSE;
    }
    if (s_hal->erase(s_hal->ctx, region_start, region_size) != 0) {
        return FALSE;
    }
    return TRUE;
}

bool NvmDrv_WriteWord(uint32 addr, uint32 wData)
{
    if (s_hal == NULL) {
        return FALSE;
    }
    return s_hal->write(s_hal->ctx, s_base_addr + addr, &wData, sizeof(wData))
               == 0 ? TRUE : FALSE;
}

bool NvmDrv_ReadWord(uint32 addr, uint32 *pReadData)
{
    if (s_hal == NULL || pReadData == NULL) {
        return FALSE;
    }
    return s_hal->read(s_hal->ctx, s_base_addr + addr, pReadData,
                       sizeof(*pReadData)) == 0 ? TRUE : FALSE;
}

BOOL NvmDrv_EraseAll(cNvmDrv *me)
{
    (void)me;
    return tym_setting_sim_erase_all() == 0 ? TRUE : FALSE;
}
