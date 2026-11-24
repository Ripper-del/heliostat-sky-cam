/** @file crc16.h — CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) для записів журналу, кадрів і калібровки. */
#ifndef CRC16_H
#define CRC16_H
#include <stdint.h>
#include <stddef.h>
uint16_t crc16(const uint8_t *data, size_t len);
#endif
