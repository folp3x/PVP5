/* i2cFram.h */
#ifndef __I2C_H
#define __I2C_H
#include <stdint.h>

// максимум 254 I2C_CR2: Bits 23:16 NBYTES[7:0]: Number of bytes - 1 байт адреса
#define IICBUFSZ 254 

extern unsigned char leni2c;	// ƒлинна прин¤того пакета I2C
void init_I2C_FRAM(void);
void* i2cFRAM_rd(uint32_t addrdat, uint32_t ubNumByteToRead);
void* i2cFRAM_wr(uint32_t addrdat, uint8_t* pBuffer, uint32_t ubNumByteToWrite);
#endif /* __MAIN_H */
