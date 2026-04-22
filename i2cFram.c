#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FRAM_SIZE 2048

static uint8_t fram_memory[FRAM_SIZE];
unsigned char leni2c = 0;

void init_I2C_FRAM(void) { memset(fram_memory, '_', FRAM_SIZE); }

void *i2cFRAM_wr(uint32_t addrdat, uint8_t *pBuffer,
                 uint32_t ubNumByteToWrite) {
  if (addrdat + ubNumByteToWrite <= FRAM_SIZE) {
    memcpy(&fram_memory[addrdat], pBuffer, ubNumByteToWrite);
    return (void *)1;
  }
  return 0;
}

void *i2cFRAM_rd(uint32_t addrdat, uint32_t ubNumByteToRead) {
  static uint8_t buffer[256];
  if (addrdat + ubNumByteToRead <= FRAM_SIZE) {
    memcpy(buffer, &fram_memory[addrdat], ubNumByteToRead);
    leni2c = ubNumByteToRead;
    return buffer;
  }
  return 0;
}