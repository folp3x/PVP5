#define _CRT_SECURE_NO_WARNINGS

#include "messages.h"
#include <stdint.h>
#include <stdio.h>

#define F_OFF 0
#define F_PEND 1
#define F_SET 2

static struct MSG_DATA {
  int Msg;
  void *ParamPtr;
} Messages[MAX_MESSAGES];

void InitMessages(void) {
  for (int i = 0; i < MAX_MESSAGES; i++)
    Messages[i].Msg = F_OFF;
}

void FSM_SendMessage(int Msg, void *ParamPtr) {
  Messages[Msg].Msg = F_PEND;
  Messages[Msg].ParamPtr = ParamPtr;
}

void ProcessMessages(void) {
  for (int i = 0; i < MAX_MESSAGES; i++) {
    if (Messages[i].Msg == F_SET)
      Messages[i].Msg = F_OFF;
    if (Messages[i].Msg == F_PEND) {
      Messages[i].Msg = F_SET;
    }
  }
}

char FSM_GetMessage(int Msg, void *ParamPtr) {
  if (Messages[Msg].Msg == F_SET) {
    *(void **)ParamPtr = Messages[Msg].ParamPtr;
    Messages[Msg].Msg = F_OFF;
    return 1;
  }
  return 0;
}