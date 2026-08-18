
/**
 * @file        SettingSrv_priv.h
 * @brief       This implement the server for all settings store and retrieve
 * @author      Wesley Lee
 * @date        2014-06-09
 * @copyright   Tymphany Ltd.
 */

#ifndef SETTINGSRV_PRIV_H
#define SETTINGSRV_PRIV_H

#ifdef __cplusplus
extern "C" {
#endif


#include "app_setting_idle_activity.h"


/* @brief the structure of the array element
 *          for the fast lookup setting entity with only ID provided
 */
typedef struct tSettingDatabase
{
    void * const    p;      /**< pointer to the object*/
    uint16          size;   /**< size of the object*/
    uint16          attr;   /**< attribute*/
}tSettingDatabase;

/*enum
{
    SETTING_TIMEOUT_SIG = MAX_SIG ,
}eSettingInternalSig;*/

#define SETTING_ATTR_NVM        0x0001      /**< the entity need to be stored in Non-Volatile Memory */
#define SETTING_ATTR_EEPROM     0x0002      /**< the entity need to be stored in EEPROM */
#define SETTING_ATTR_SET        0x2000      /**< the entity is being set already */
#define SETTING_ATTR_VALID      0x4000      /**< the entity is valid for the product */
#define SETTING_ATTR_UPD        0x8000      /*setting update mask if set,- data entry was updated in RAM */

#define NO_OFFSET               0

#ifdef SETTING_HAS_ROM_DATA
static void SettingSrv_Read(cSettingSrv * const me, uint8* pData, uint16 size, uint16 offset);
static void SettingSrv_Write(cSettingSrv * const me, uint8* pData, uint16 size, uint16 offset);
static void SettingSrv_LoadStoredValue(cSettingSrv * const me);
static void SettingSrv_Bookkeeping(cSettingSrv * const me);
#endif
/* getRomAddr 为 core .c 内部 static，无需在此声明 */

/* State function definitions */
#ifdef SETTING_HAS_ROM_DATA
QState SettingSrv_Initial(cSettingSrv * const me, QEvt const * const e);
QState SettingSrv_Active(cSettingSrv * const me, QEvt const * const e);
QState SettingSrv_BusyRead(cSettingSrv * const me, QEvt const * const e);
QState SettingSrv_BusyWrite(cSettingSrv * const me, QEvt const * const e);
QState SettingSrv_DeActive(cSettingSrv * const me, QEvt const * const e);
#endif

#if defined(ENABLE_SETTING_UPDATE_PUBLISH) || defined(SETTING_HAS_ROM_DATA)
static void SettingSrv_SettUpdateNotification(eSettingId setting_id, uint16 server_id);
#endif
#ifdef __cplusplus
}
#endif

#endif /* SETTINGSRV_PRIV_H */

