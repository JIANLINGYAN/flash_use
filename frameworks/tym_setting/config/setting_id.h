/* =====================================================================
 * TYM Setting 业务 ID 枚举（精简演示集）
 *
 * 相对原版 438 项大幅精简，仅保留代表不同类型/属性的少量 ID，
 * 用于验证框架的读写改、NVM/非 NVM 属性差异、大小不齐整等特性。
 * 移植到真实产品时在此扩展（SETID_MAX 必须最后，settingDB 尺寸依赖它）。
 * ===================================================================== */

#ifndef SETTING_ID_H
#define SETTING_ID_H

typedef enum
{
    SETID_START = 0,

    /* 需落 Flash 的持久项 */
    SETID_MASTER_VOL = SETID_START,  /* uint32, 4 字节对齐 */
    SETID_AUDIO_GAIN_1,              /* uint32, 4 字节（演示多 NVM 槽位） */
    SETID_AUDIO_GAIN_2,              /* uint32, 4 字节 */
    SETID_AUDIO_GAIN_3,              /* uint32, 4 字节 */
    SETID_FACTORY_RESET,             /* uint8,  <4 字节，走补齐分支 */
    SETID_OTA_FLAG,                  /* uint8,  <4 字节 */
    SETID_BT_NAME,                   /* char[24], 24 字节 4 对齐 */

    /* 仅 RAM 有效（VALID 但非 NVM，不落 Flash） */
    SETID_CH1_SIG_LEVEL,

    SETID_MAX                        /* 必须最后 */
} eSettingId;

#endif /* SETTING_ID_H */
