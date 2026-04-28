#define _CRT_SECURE_NO_WARNINGS

#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "fsm_tm.h"
#include "i2cFram.h"
#include "keypad.h"
#include "messages.h"
#include "timers.h"
#include "uart.h"

int lenfsm = 0;

static char tm_cmd_buffer1[32];
static char tm_cmd_buffer2[32];

extern void InitTM(void);
extern void EmulateKeyPress(uint16_t);
extern char are_both_mt_stopped(void);

void StartMT1(uint16_t start_addr, int steps) {
  sprintf(tm_cmd_buffer1, "%x:%d", start_addr, steps);
  FSM_SendMessage(MSG_TM1STRT, tm_cmd_buffer1);
}

void StartMT2(uint16_t start_addr, int steps) {
  sprintf(tm_cmd_buffer2, "%x:%d", start_addr, steps);
  FSM_SendMessage(MSG_TM2STRT, tm_cmd_buffer2);
}

void StartBothMT(uint16_t addr1, int steps1, uint16_t addr2, int steps2) {
  sprintf(tm_cmd_buffer1, "%x:%d", addr1, steps1);
  sprintf(tm_cmd_buffer2, "%x:%d", addr2, steps2);
  FSM_SendMessage(MSG_TM1STRT, tm_cmd_buffer1);
  FSM_SendMessage(MSG_TM2STRT, tm_cmd_buffer2);
}

void print_both_tapes(void) {
  printf("\nTapes: ");

  for (int i = 0; i < 8; i++) {
    uint8_t *val = (uint8_t *)i2cFRAM_rd(i, 1);
    printf("%c", val ? val[0] : '?');
  }
  printf("|");

  for (int i = 8; i < 16; i++) {
    uint8_t *val = (uint8_t *)i2cFRAM_rd(i, 1);
    printf("%c", val ? val[0] : '?');
  }
  printf("\n");
}

int main(void) {
  setlocale(LC_ALL, "rus");
  SetConsoleOutputCP(CP_UTF8);

  InitMessages();
  InitTimers();
  init_I2C_FRAM();
  InitTM();

  ///* пример с зависанием МТ1 */
  //// Лента МТ1 (адреса 0-7)
  // uint8_t tape1[] = { '_', '1', '0', '0', '_', '_', '_', '_' };
  // i2cFRAM_wr(0, tape1, 8);

  //// Лента МТ2 (адреса 8-15)
  // uint8_t tape2[] = { '_', '3', '2', '1', '_', '_', '_', '_' };
  // i2cFRAM_wr(8, tape2, 8);

  //// Запуск обеих МТ
  // StartMT1(1, 200);   // МТ1 с адреса 1
  // StartMT2(9, 200);   // МТ2 с адреса 9 (8 + 1)

  // EmulateKeyPress(1 << 1);

  ///* пример с cинхронной работой МТ */
  //// Лента МТ1 (адреса 0-7)
  // uint8_t tape1[] = { '_', '_', '_', '_', '_', '_', '1', '2' };
  // i2cFRAM_wr(0, tape1, 8);

  //// Лента МТ2 (адреса 8-15)
  // uint8_t tape2[] = { '3', '4', '_', '_', '_', '_', '_', '_' };
  // i2cFRAM_wr(8, tape2, 8);

  //// Запуск МТ1
  // StartMT1(6, 200);   // МТ1 с адреса 1
  // StartMT2(8, 200);

  /* пример запуска МТ в параллельную работу */
  // Лента МТ1 (адреса 0-7)
  uint8_t tape1[] = {'_', '2', '0', '0', '0', '0', '0', '0'};
  i2cFRAM_wr(0, tape1, 8);

  // Лента МТ2 (адреса 8-15)
  uint8_t tape2[] = {'0', '_', '_', '_', '_', '_', '_', '_'};
  i2cFRAM_wr(8, tape2, 8);

  // Запуск МТ
  StartBothMT(1, 200, 9, 200);

  while (1) {
    ProcessMessages();
    ProcessFsmTm();
    ProcessKeyKpd4();

    if (are_both_mt_stopped()) {
      print_both_tapes();
      break;
    }
  }

  return 0;
}