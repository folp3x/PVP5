#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fsm_tm.h"
#include "i2cFram.h"
#include "keypad.h"
#include "messages.h"
#include "timers.h"
#include "uart.h"

#define FSMST_Q0 0
#define FSMST_Q1 1
#define FSMST_Q2 2
#define FSMST_Q3 3
#define FSMST_Q4 4
#define FSMST_Q5 5

#define MTCTRL_RXCMD 0
#define MTCTRL_PROC 1
#define MTCTRL_RDFRAM 2
#define MTCTRL_WRFRAM 3
#define MTCTRL_UARTTR 4
#define MTCTRL_SYNCWAIT 5

#define FRAM_MAX_ADDR 0x7FF
#define MT1_BASE_ADDR 0
#define MT2_BASE_ADDR 8
#define TAPE_SIZE 8

struct TM_Context {
  uint32_t curraddr;
  uint32_t nextaddr;
  int steptm;
  int stepcnt;
  char mtstatectrl;
  char fsmstate;
  uint8_t base_addr;
  char timer_id;
  char msg_id;
  char name[4];
  uint8_t rdsymbol;
  uint8_t wrsymbol;
  int hang_counter;
  uint32_t last_addr;
  char last_state;
  char last_transition_info[256];
};

static struct TM_Context mt1, mt2;

uint8_t normalize_symbol(uint8_t sym) {
  if (sym >= '0' && sym <= '9')
    return sym;
  return '_';
}

void get_tape_str(char *buffer, int buf_size, uint8_t base_addr) {
  char *ptr = buffer;
  for (int i = 0; i < TAPE_SIZE; i++) {
    uint8_t *val = (uint8_t *)i2cFRAM_rd(base_addr + i, 1);
    ptr += snprintf(ptr, buf_size - (ptr - buffer), "%c", val ? val[0] : '?');
  }
}

void send_via_uart(const char *str) {
  uart_transmit((uint8_t *)str, (uint8_t)strlen(str));
}

int ProcTmStep(struct TM_Context *ctx, uint8_t rsymb, uint8_t *wrsymb) {
  static char sync_data[16];

  *wrsymb = rsymb;
  ctx->stepcnt++;

  char tape_str[16];
  get_tape_str(tape_str, sizeof(tape_str), ctx->base_addr);

  char transition[256];
  transition[0] = '\0';

  switch (ctx->fsmstate) {
  case FSMST_Q0:
    if (rsymb != '_') {
      ctx->nextaddr = ctx->curraddr + 1;
      ctx->fsmstate = FSMST_Q0;
      snprintf(transition, sizeof(transition), "- Q0 %s [!_ ; ~ , R]",
               tape_str);
    } else {
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q1;
      snprintf(transition, sizeof(transition), "- Q0 %s [_ ; _ , L]", tape_str);
    }
    break;

  case FSMST_Q1:
    if (rsymb == '1') {
      *wrsymb = '0';
      ctx->fsmstate = FSMST_Q5;
      snprintf(transition, sizeof(transition), "- Q1 %s [1 ; 0 , S]", tape_str);
    } else {
      ctx->fsmstate = FSMST_Q2;
      snprintf(transition, sizeof(transition), "- Q1 %s [!1 ; ~ , S]",
               tape_str);
    }
    break;

  case FSMST_Q2:
    if (rsymb == '0') {
      *wrsymb = '9';
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q2;
      snprintf(transition, sizeof(transition), "- Q2 %s [0 ; 9 , L]", tape_str);
    } else if (rsymb >= '1' && rsymb <= '9') {
      *wrsymb = rsymb - 1;
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q3;
      snprintf(transition, sizeof(transition), "- Q2 %s [%c ; %c , L]",
               tape_str, rsymb, *wrsymb);
    } else {
      snprintf(transition, sizeof(transition), "- Q2: %s [undefined]",
               tape_str);
    }
    break;

  case FSMST_Q3:
    if (rsymb != '_') {
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q3;
      snprintf(transition, sizeof(transition), "- Q3 %s [!_ ; ~ , L]",
               tape_str);
    } else {
      ctx->nextaddr = ctx->curraddr + 1;
      ctx->fsmstate = FSMST_Q4;
      snprintf(transition, sizeof(transition), "- Q3 %s [_ ; _ , R]", tape_str);
    }
    break;

  case FSMST_Q4:
    if (rsymb == '0') {
      *wrsymb = '_';
      ctx->nextaddr = ctx->curraddr + 1;
      ctx->fsmstate = FSMST_Q4;
      snprintf(transition, sizeof(transition), "- Q4 %s [0 ; _ , R]", tape_str);
    } else {
      ctx->fsmstate = FSMST_Q5;
      snprintf(transition, sizeof(transition), "- Q4 %s [!0 ; ~ , S]",
               tape_str);
    }
    break;

  case FSMST_Q5:
    ctx->stepcnt = -1;
    snprintf(transition, sizeof(transition), "- Q5 %s", tape_str);
    break;

  default:
    ctx->stepcnt = -1;
    break;
  }

  strncpy(ctx->last_transition_info, transition,
          sizeof(ctx->last_transition_info) - 1);
  ctx->last_transition_info[sizeof(ctx->last_transition_info) - 1] = '\0';

  // МТ1: правая граница -> переход в SYNCWAIT
  if (ctx->base_addr == MT1_BASE_ADDR &&
      ctx->nextaddr >= MT1_BASE_ADDR + TAPE_SIZE) {
    snprintf(sync_data, sizeof(sync_data), "%d:%d", (int)ctx->fsmstate,
             (int)ctx->nextaddr);
    FSM_SendMessage(MSG_SYNC_MT1_TO_MT2, sync_data);
    ctx->mtstatectrl = MTCTRL_SYNCWAIT;
    ctx->stepcnt = -1;
    return -1;
  }

  // МТ2: левая граница -> переход в SYNCWAIT
  if (ctx->base_addr == MT2_BASE_ADDR && ctx->nextaddr < MT2_BASE_ADDR) {
    snprintf(sync_data, sizeof(sync_data), "%d:%d", (int)ctx->fsmstate,
             (int)ctx->nextaddr);
    FSM_SendMessage(MSG_SYNC_MT2_TO_MT1, sync_data);
    ctx->mtstatectrl = MTCTRL_SYNCWAIT;
    ctx->stepcnt = -1;
    return -1;
  }

  if (ctx->curraddr == ctx->last_addr && ctx->fsmstate == ctx->last_state) {
    ctx->hang_counter++;
    if (ctx->hang_counter > 1) {
      snprintf(transition, sizeof(transition), "%s>hang detected\n", ctx->name);
      send_via_uart(transition);
      ctx->stepcnt = -1;
      return -1;
    }
  } else {
    ctx->hang_counter = 0;
    ctx->last_addr = ctx->curraddr;
    ctx->last_state = ctx->fsmstate;
  }

  return ctx->stepcnt;
}

void ProcessOneTm(struct TM_Context *ctx) {
  static uint8_t aTrbuf[256];
  uint8_t *pRxbuf = 0;
  char buf[256];

  // Обработка нажатий кнопок
  if (FSM_GetMessage(MSG_KEYPRESSED, (void *)&pRxbuf)) {
    if (pRxbuf != NULL) {
      uint16_t keycode = *(uint16_t *)pRxbuf;
      uint8_t blank = '_';
      for (int i = 0; i < 16; i++) {
        if (keycode & (1 << i)) {
          uint32_t addr = (i < 8) ? MT1_BASE_ADDR + i : MT2_BASE_ADDR + (i - 8);
          i2cFRAM_wr(addr, &blank, 1);
          snprintf(buf, sizeof(buf), "%s>fail addr %d (K%d pressed)\n",
                   ctx->name, addr, i + 1);
          send_via_uart(buf);
        }
      }
    }
  }

  // МТ2 получает сообщение от МТ1
  if (ctx->msg_id == MSG_TM2STRT) {
    if (FSM_GetMessage(MSG_SYNC_MT1_TO_MT2, (void *)&pRxbuf)) {
      int state, nextaddr;
      sscanf((char *)pRxbuf, "%d:%d", &state, &nextaddr);
      ctx->fsmstate = (char)state;
      ctx->nextaddr = nextaddr;
      ctx->mtstatectrl = MTCTRL_RDFRAM;
      ctx->stepcnt = 0;
      ResetTimer(ctx->timer_id);
      snprintf(buf, sizeof(buf),
               "%s>sync restored, state=%d, addr=0x%03X, steps=%d\n", ctx->name,
               state, ctx->nextaddr, ctx->steptm);
      send_via_uart(buf);
      return;
    }
  }

  // МТ1 получает сообщение от МТ2
  if (ctx->msg_id == MSG_TM1STRT) {
    if (FSM_GetMessage(MSG_SYNC_MT2_TO_MT1, (void *)&pRxbuf)) {
      int state, nextaddr;
      sscanf((char *)pRxbuf, "%d:%d", &state, &nextaddr);
      ctx->fsmstate = (char)state;
      ctx->nextaddr = nextaddr;
      ctx->mtstatectrl = MTCTRL_RDFRAM;
      ctx->stepcnt = 0;
      ResetTimer(ctx->timer_id);
      snprintf(buf, sizeof(buf),
               "%s>sync restored, state=%d, addr=0x%03X, steps=%d\n", ctx->name,
               state, ctx->nextaddr, ctx->steptm);
      send_via_uart(buf);
      return;
    }
  }

  // Если МТ в состоянии SYNCWAIT и не получила сообщение
  if (ctx->mtstatectrl == MTCTRL_SYNCWAIT) {
    snprintf(buf, sizeof(buf), "%s>waiting for sync\n", ctx->name);
    send_via_uart(buf);
    return;
  }

  switch (ctx->mtstatectrl) {
  case MTCTRL_RXCMD:
    if (FSM_GetMessage(ctx->msg_id, (void *)&pRxbuf)) {
      char local_cmd[32];
      strncpy(local_cmd, (char *)pRxbuf, 31);
      local_cmd[31] = '\0';
      ctx->nextaddr = 0;
      ctx->steptm = 0;
      sscanf(local_cmd, "%x:%d", &ctx->nextaddr, &ctx->steptm);

      if ((ctx->base_addr == MT1_BASE_ADDR &&
           ctx->nextaddr >= MT1_BASE_ADDR + TAPE_SIZE) ||
          (ctx->base_addr == MT2_BASE_ADDR && ctx->nextaddr < MT2_BASE_ADDR)) {

        snprintf(buf, sizeof(buf), "%s: start address out of tape bounds!\n",
                 ctx->name);
        send_via_uart(buf);
        snprintf((char *)aTrbuf, sizeof(aTrbuf),
                 "%s Error: address out of tape bounds!\n", ctx->name);
        ctx->mtstatectrl = MTCTRL_UARTTR;
        ctx->steptm = -1;
        break;
      }

      if (ctx->nextaddr <= FRAM_MAX_ADDR && ctx->steptm > 0) {
        ctx->mtstatectrl = MTCTRL_RDFRAM;
        ctx->fsmstate = FSMST_Q0;
        ctx->stepcnt = 0;
        ResetTimer(ctx->timer_id);
      } else {
        snprintf((char *)aTrbuf, sizeof(aTrbuf), "%s Error!\n", ctx->name);
        ctx->mtstatectrl = MTCTRL_UARTTR;
        ctx->steptm = -1;
      }
    }
    break;

  case MTCTRL_RDFRAM:
    if (ctx->steptm <= 0) {
      ctx->mtstatectrl = MTCTRL_RXCMD;
      break;
    }

    ctx->curraddr = ctx->nextaddr;
    if (ctx->curraddr > FRAM_MAX_ADDR) {
      snprintf((char *)aTrbuf, sizeof(aTrbuf),
               "%s Error: address out of bounds\n", ctx->name);
      ctx->mtstatectrl = MTCTRL_UARTTR;
      ctx->steptm = -1;
      break;
    }
    pRxbuf = (uint8_t *)i2cFRAM_rd(ctx->curraddr, 1);
    if (pRxbuf) {
      ctx->rdsymbol = normalize_symbol(pRxbuf[0]);
      ctx->mtstatectrl = MTCTRL_PROC;
    }
    break;

  case MTCTRL_PROC: {
    if (ctx->steptm <= 0) {
      ctx->mtstatectrl = MTCTRL_RXCMD;
      break;
    }

    int result = ProcTmStep(ctx, ctx->rdsymbol, &ctx->wrsymbol);

    if (result == -1) {
      snprintf((char *)aTrbuf, sizeof(aTrbuf),
               "%s>stop addr=%03X sym='%c' %s\n", ctx->name, ctx->curraddr,
               ctx->wrsymbol, ctx->last_transition_info);
      ctx->steptm = -1;
    } else {
      snprintf((char *)aTrbuf, sizeof(aTrbuf),
               "%s>step=%d addr=%03X sym='%c' %s\n", ctx->name, result,
               ctx->curraddr, ctx->wrsymbol, ctx->last_transition_info);
    }
    send_via_uart((char *)aTrbuf);

    if (ctx->wrsymbol == ctx->rdsymbol) {
      ctx->mtstatectrl = MTCTRL_UARTTR;
    } else {
      ctx->mtstatectrl = MTCTRL_WRFRAM;
    }
    break;
  }

  case MTCTRL_WRFRAM:
    if (ctx->curraddr <= FRAM_MAX_ADDR) {
      i2cFRAM_wr(ctx->curraddr, &ctx->wrsymbol, 1);
    }
    ctx->mtstatectrl = MTCTRL_UARTTR;
    break;

  case MTCTRL_UARTTR:
    if (GetTimer(ctx->timer_id) > 250) {
      ResetTimer(ctx->timer_id);
      if (ctx->steptm == -1) {
        ctx->mtstatectrl = MTCTRL_RXCMD;
      } else {
        ctx->steptm--;
        if (ctx->steptm > 0) {
          ctx->mtstatectrl = MTCTRL_RDFRAM;
        } else {
          snprintf(buf, sizeof(buf), "%s>steps expired\n", ctx->name);
          send_via_uart(buf);
          ctx->mtstatectrl = MTCTRL_SYNCWAIT;
          ctx->steptm = -1;
        }
      }
    }
    break;
  }
}

void ProcessFsmTm(void) {
  ProcessOneTm(&mt1);
  ProcessOneTm(&mt2);
}

void InitTM(void) {
  mt1.curraddr = 0;
  mt1.nextaddr = 0;
  mt1.steptm = 0;
  mt1.stepcnt = 0;
  mt1.mtstatectrl = MTCTRL_RXCMD;
  mt1.fsmstate = FSMST_Q0;
  mt1.base_addr = MT1_BASE_ADDR;
  mt1.timer_id = TM1_TIMER;
  mt1.msg_id = MSG_TM1STRT;
  mt1.rdsymbol = 0;
  mt1.wrsymbol = 0;
  mt1.hang_counter = 0;
  mt1.last_addr = 0;
  mt1.last_state = 0;
  strcpy(mt1.name, "TM1");

  mt2.curraddr = 0;
  mt2.nextaddr = 0;
  mt2.steptm = 0;
  mt2.stepcnt = 0;
  mt2.mtstatectrl = MTCTRL_RXCMD;
  mt2.fsmstate = FSMST_Q0;
  mt2.base_addr = MT2_BASE_ADDR;
  mt2.timer_id = TM2_TIMER;
  mt2.msg_id = MSG_TM2STRT;
  mt2.rdsymbol = 0;
  mt2.wrsymbol = 0;
  mt2.hang_counter = 0;
  mt2.last_addr = 0;
  mt2.last_state = 0;
  strcpy(mt2.name, "TM2");
}

char are_both_mt_stopped(void) {
  return (mt1.mtstatectrl == MTCTRL_RXCMD && mt2.mtstatectrl == MTCTRL_RXCMD);
}