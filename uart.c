#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>

#include "uart.h"

unsigned char uart_transmit(uint8_t *pBuffer, uint8_t ublenBuff) {
  for (int i = 0; i < ublenBuff; i++) {
    putchar(pBuffer[i]);
  }
  return 0;
}