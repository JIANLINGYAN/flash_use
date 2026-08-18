/*
 * Copyright (c) 2026 flash_use 平台
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 兼容层：zephyr/sys/crc.h 最小模拟。
 * 仅声明 FCB/NVS/ZMS 实际使用的 CRC 函数与初始值。
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_SYS_CRC_H_
#define ZEPHYR_INCLUDE_ZEPHYR_SYS_CRC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC8_CCITT_INITIAL_VALUE 0xFF

/**
 * Compute CCITT variant of CRC 8 (poly 0x07, MSB-first, no reflection).
 * @param initial_value CRC seed
 * @param buf input data
 * @param len data length
 * @return CRC8 value
 */
uint8_t crc8_ccitt(uint8_t initial_value, const void *buf, size_t len);

/**
 * Compute IEEE conform CRC32 checksum (poly 0xEDB88320, reflected).
 * @param data input data
 * @param len data length
 * @return CRC32 value
 */
uint32_t crc32_ieee(const uint8_t *data, size_t len);

/**
 * Update an IEEE conforming CRC32 checksum.
 * @param crc previous CRC32 value (0 for the first block)
 * @param data input data
 * @param len data length
 * @return updated CRC32 value
 */
uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_SYS_CRC_H_ */
