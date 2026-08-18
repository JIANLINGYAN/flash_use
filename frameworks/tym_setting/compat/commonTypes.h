/******************************************************************************

Copyright (c) 2015, Tymphany HK Ltd. All rights reserved.

Confidential information of Tymphany HK Ltd.

The Software is provided "AS IS" and "WITH ALL FAULTS," without warranty of any
kind, including without limitation the warranties of merchantability, fitness
for a particular purpose and non-infringement. Tymphany HK LTD. makes no
warranty that the Software is free of defects or is suitable for any particular
purpose.

******************************************************************************/

/**
*  @file      commonTypes.h
*  @brief     This is the base template class for hw drivers.
*  @author    Christopher Alexander
*  @date      19-Jul-2013
*  @copyright Tymphany Ltd.
*/

#ifndef COMMONTYPES_H
#define COMMONTYPES_H

#ifdef __cplusplus
extern "C" {
#endif




#include <string.h>
#include <stdbool.h>

typedef unsigned char  uchar;  /*<! unsigned char   8 bits [0,255] */
typedef unsigned char  uint8;  /*<! unsigned char   8 bits [0,255] */
typedef signed   char  int8;   /*<! signed   char   8 bits [-128,127] */
typedef unsigned short uint16; /*<! unsigned short 16 bits [0,65535]*/
typedef signed   short int16;  /*<! signed   short 16 bits [-32768,32767]*/
typedef unsigned long  uint32; /*<! unsigned long  32 bits [0,4294967295]*/
typedef signed   long  int32;  /*<! signed   long  32 bits [-2147483648, 2147483647]*/
typedef signed long long   int64; /**< C99 exact-width 64-bit signed int */
typedef unsigned long long uint64; /**< C99 exact-width 64-bit unsigned int */
typedef float float32_t;


#ifndef BOOL 
#define BOOL bool
#endif

#ifndef TRUE
#define TRUE true
#endif

#ifndef FALSE
#define FALSE false
#endif



/* Add general "compiler safe" common types here */

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define MIN3(a,b,c) (((a)<(b))?( MIN(a,c) ):( MIN(b,c) ))
#define MAX3(a,b,c) (((a)>(b))?( MAX(a,c) ):( MAX(b,c) ))

#define MIN4(a,b,c,d)  MIN( MIN(a,b), MIN(c,d) )
#define MAX4(a,b,c,d)  MAX( MAX(a,b), MAX(c,d) )

#define MIN6(a,b,c,d,e,f)  MIN( MIN3(a,b,c), MIN3(e,d,f) )
#define MAX6(a,b,c,d,e,f)  MAX( MAX3(a,b,c), MAX3(e,d,f) )

#define TYM_SET_BIT(v, f) ((v) |= (f))
#define TYM_CLR_BIT(v, f) ((v) &= ~(f))

#define TYM_BITS_NUM_OF_ONE_BYTE  (8)
#define TYM_SIZE_OF_IN_BIT_NUM(v) (sizeof(v)*TYM_BITS_NUM_OF_ONE_BYTE)

#define TYM_DIFF(a,b) (((a)>(b))?((a)-(b)):((b)-(a)))

#define SET_BIT(REG, BIT)     ((REG) |= (BIT))

#define CLEAR_BIT(REG, BIT)   ((REG) &= ~(BIT))

#define READ_BIT(REG, BIT)    ((REG) & (BIT))

#define CLEAR_REG(REG)        ((REG) = (0x0))

#define WRITE_REG(REG, VAL)   ((REG) = (VAL))

#define READ_REG(REG)         ((REG))

#define MODIFY_REG(REG, CLEARMASK, SETMASK)  WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

typedef enum
{
    RET_SUCCESS,
    RET_FAIL,
    RET_NOT_SUPPORTED,
    RET_BUSY,
}eEvtReturn;


typedef enum
{
    TP_SUCCESS= 0,
    TP_FAIL= 1,
    TP_ACCESS_ERROR= -1,
}eTpRet;

#define INVALID_VALUE (-1)

#define ArraySize(a)  (sizeof(a) / sizeof(a[0]))

#define UINT16_BYTE_INV(buf) \
  (((uint16)(((uint8*)&buf)[0])<<8)  | \
   ((uint16)(((uint8*)&buf)[1])<<0) )

#define UINT32_BYTE_INV(buf) \
  (((uint32)(((uint8*)buf)[0])<<24) | \
   ((uint32)(((uint8*)buf)[1])<<16) | \
   ((uint32)(((uint8*)buf)[2])<<8)  | \
   ((uint32)(((uint8*)buf)[3])<<0) )

#define U16_LOW_BYTE(x)   ((uint8_t)((x) & 0x00FF))
#define U16_HIGH_BYTE(x)  ((uint8_t)(((x) >> 8) & 0x00FF))

/* Get bit range from a 4bytes value, for example,
 *   UINT32_GET_BIT_RANGE(0x12345678, 7,  0)  ==>  return 0x8
 *   UINT32_GET_BIT_RANGE(0x12345678, 15, 8)  ==>  return 0x7
 */
#define UINT32_GET_BIT_RANGE(ver, high_pos, low_pos) \
   ((((uint32)ver)<<(32-(high_pos)-1))>>(32-(high_pos)+(low_pos)-1))
   
/* Get bit range from a 4bytes value, for example,
 *   UINT32_GET_BIT_RANGE(0x12345678, 3)  ==>  return 0x1
 *   UINT32_GET_BIT_RANGE(0x12345678, 2)  ==>  return 0x0
 */ 
#define UINT32_GET_BIT(ver, pos) \
   ((((uint32)ver)>>(pos)) & 1)

   
typedef int32 (*ptrTrans)(uint8 *buf, uint16 len);

#ifdef __cplusplus
}
#endif

#endif
