#define _CRT_SECURE_NO_WARNINGS

#include "keypad.h"
#include "messages.h"
#include <stdint.h>
#include <stdio.h>

static uint16_t fake_key = 0;

uint16_t scankeypad(void) { return fake_key; }

void ProcessKeyKpd4(void) {
  if (fake_key != 0) {
    static uint16_t saved_key;
    saved_key = fake_key;
    FSM_SendMessage(MSG_KEYPRESSED, (void *)&saved_key);
    fake_key = 0;
  }
}

void EmulateKeyPress(uint16_t key_code) { fake_key = key_code; }