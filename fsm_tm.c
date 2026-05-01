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
  char boundary_reached;
  char button_pending;
  uint32_t button_addr;
  uint8_t button_symbol;
  char pending_msg[256];
  char msg_ready;
  char stop_requested; // флаг, что МТ должна остановиться
};

static struct TM_Context mt1, mt2;

uint8_t normalize_symbol(uint8_t sym) {
  if (sym >= '0' && sym <= '9')
    return sym;
  return '_';
}

int ProcTmStep(struct TM_Context *ctx, uint8_t rsymb, uint8_t *wrsymb) {
  static char sync_data[16];

  *wrsymb = rsymb;
  ctx->stepcnt++;

  char transition[256];
  transition[0] = '\0';

  switch (ctx->fsmstate) {
  case FSMST_Q0:
    if (rsymb != '_') {
      ctx->nextaddr = ctx->curraddr + 1;
      ctx->fsmstate = FSMST_Q0;
      snprintf(transition, sizeof(transition), "- Q0 [!_ ; ~ , R]");
    } else {
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q1;
      snprintf(transition, sizeof(transition), "- Q0 [_ ; _ , L]");
    }
    break;

  case FSMST_Q1:
    if (rsymb == '1') {
      *wrsymb = '0';
      ctx->fsmstate = FSMST_Q5;
      snprintf(transition, sizeof(transition), "- Q1 [1 ; 0 , S]");
    } else {
      ctx->fsmstate = FSMST_Q2;
      snprintf(transition, sizeof(transition), "- Q1 [!1 ; ~ , S]");
    }
    break;

  case FSMST_Q2:
    if (rsymb == '0') {
      *wrsymb = '9';
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q2;
      snprintf(transition, sizeof(transition), "- Q2 [0 ; 9 , L]");
    } else if (rsymb >= '1' && rsymb <= '9') {
      *wrsymb = rsymb - 1;
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q3;
      snprintf(transition, sizeof(transition), "- Q2 [%c ; %c , L]", rsymb,
               *wrsymb);
    } else {
      snprintf(transition, sizeof(transition), "- Q2: [undefined]");
    }
    break;

  case FSMST_Q3:
    if (rsymb != '_') {
      ctx->nextaddr = ctx->curraddr - 1;
      ctx->fsmstate = FSMST_Q3;
      snprintf(transition, sizeof(transition), "- Q3 [!_ ; ~ , L]");
    } else {
      ctx->nextaddr = ctx->curraddr + 1;
      ctx->fsmstate = FSMST_Q4;
      snprintf(transition, sizeof(transition), "- Q3 [_ ; _ , R]");
    }
    break;

  case FSMST_Q4:
    if (rsymb == '0') {
      *wrsymb = '_';
      ctx->nextaddr = ctx->curraddr + 1;
      ctx->fsmstate = FSMST_Q4;
      snprintf(transition, sizeof(transition), "- Q4 [0 ; _ , R]");
    } else {
      ctx->fsmstate = FSMST_Q5;
      snprintf(transition, sizeof(transition), "- Q4 [!0 ; ~ , S]");
    }
    break;

  case FSMST_Q5:
    snprintf(transition, sizeof(transition), "- Q5");

    if (ctx->base_addr == MT1_BASE_ADDR) {
      FSM_SendMessage(MSG_SYNC_MT1_TO_MT2, "STOP");
    } else {
      FSM_SendMessage(MSG_SYNC_MT2_TO_MT1, "STOP");
    }
    return -1;
    break;

  default:
    break;
  }

  strncpy(ctx->last_transition_info, transition,
          sizeof(ctx->last_transition_info) - 1);
  ctx->last_transition_info[sizeof(ctx->last_transition_info) - 1] = '\0';

  // МТ1: правая граница
  if (ctx->base_addr == MT1_BASE_ADDR &&
      ctx->nextaddr >= MT1_BASE_ADDR + TAPE_SIZE) {
    snprintf(sync_data, sizeof(sync_data), "%d:%d", (int)ctx->fsmstate,
             (int)ctx->nextaddr);
    FSM_SendMessage(MSG_SYNC_MT1_TO_MT2, sync_data);
    return -2;
  }

  // МТ2: левая граница
  if (ctx->base_addr == MT2_BASE_ADDR && ctx->nextaddr < MT2_BASE_ADDR) {
    snprintf(sync_data, sizeof(sync_data), "%d:%d", (int)ctx->fsmstate,
             (int)ctx->nextaddr);
    FSM_SendMessage(MSG_SYNC_MT2_TO_MT1, sync_data);
    return -2;
  }

  if (ctx->curraddr == ctx->last_addr && ctx->fsmstate == ctx->last_state) {
    ctx->hang_counter++;
    if (ctx->hang_counter > 1) {
      return -3;
    }
  } else {
    ctx->hang_counter = 0;
    ctx->last_addr = ctx->curraddr;
    ctx->last_state = ctx->fsmstate;
  }

  return ctx->stepcnt;
}

void ProcessOneTm(struct TM_Context *ctx) {
  uint8_t *pRxbuf = 0;

  // обработка нажатий кнопок
  if (FSM_GetMessage(MSG_KEYPRESSED, (void *)&pRxbuf)) {
    if (pRxbuf != NULL) {
      uint16_t keycode = *(uint16_t *)pRxbuf;
      for (int i = 0; i < 16; i++) {
        if (keycode & (1 << i)) {
          uint32_t addr = (i < 8) ? MT1_BASE_ADDR + i : MT2_BASE_ADDR + (i - 8);
          // Проверяем, принадлежит ли адрес этой МТ
          if (addr >= ctx->base_addr && addr < ctx->base_addr + TAPE_SIZE) {
            ctx->button_addr = addr;
            ctx->button_symbol = '_';
            ctx->button_pending = 1;
            // Прерываем текущую операцию для обработки кнопки
            ctx->mtstatectrl = MTCTRL_WRFRAM;
            return;
          }
        }
      }
    }
  }

  // МТ2 получает сообщение от МТ1
  if (ctx->msg_id == MSG_TM2STRT) {
    if (FSM_GetMessage(MSG_SYNC_MT1_TO_MT2, (void *)&pRxbuf)) {
      if (strcmp((char *)pRxbuf, "STOP") == 0) {
        ctx->mtstatectrl = MTCTRL_RXCMD;
        ctx->steptm = -1;
        ctx->stop_requested = 1;
        return;
      }
      int state, nextaddr;
      sscanf((char *)pRxbuf, "%d:%d", &state, &nextaddr);
      ctx->fsmstate = (char)state;
      ctx->nextaddr = nextaddr;
      ctx->boundary_reached = 0;
      ctx->stop_requested = 0;
      ctx->mtstatectrl = MTCTRL_RDFRAM;
      ResetTimer(ctx->timer_id);
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s>sync restored, state=%d, addr=0x%03X, steps=%d\n", ctx->name,
               state, ctx->nextaddr, ctx->stepcnt);
      ctx->msg_ready = 1;
      ctx->mtstatectrl = MTCTRL_UARTTR;
      return;
    }
  }

  // МТ1 получает сообщение от МТ2
  if (ctx->msg_id == MSG_TM1STRT) {
    if (FSM_GetMessage(MSG_SYNC_MT2_TO_MT1, (void *)&pRxbuf)) {
      if (strcmp((char *)pRxbuf, "STOP") == 0) {
        ctx->mtstatectrl = MTCTRL_RXCMD;
        ctx->steptm = -1;
        ctx->stop_requested = 1;
        return;
      }
      int state, nextaddr;
      sscanf((char *)pRxbuf, "%d:%d", &state, &nextaddr);
      ctx->fsmstate = (char)state;
      ctx->nextaddr = nextaddr;
      ctx->boundary_reached = 0;
      ctx->stop_requested = 0;
      ctx->mtstatectrl = MTCTRL_RDFRAM;
      ResetTimer(ctx->timer_id);
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s>sync restored, state=%d, addr=0x%03X, steps=%d\n", ctx->name,
               state, ctx->nextaddr, ctx->stepcnt);
      ctx->msg_ready = 1;
      ctx->mtstatectrl = MTCTRL_UARTTR;
      return;
    }
  }

  switch (ctx->mtstatectrl) {
  case MTCTRL_RXCMD:
    if (FSM_GetMessage(ctx->msg_id, (void *)&pRxbuf)) {
      char local_cmd[32];
      strncpy(local_cmd, (char *)pRxbuf, 31);
      local_cmd[31] = '\0';
      ctx->nextaddr = 0;
      int received_steps = 0;
      sscanf(local_cmd, "%x:%d", &ctx->nextaddr, &received_steps);

      if ((ctx->base_addr == MT1_BASE_ADDR &&
           ctx->nextaddr >= MT1_BASE_ADDR + TAPE_SIZE) ||
          (ctx->base_addr == MT2_BASE_ADDR && ctx->nextaddr < MT2_BASE_ADDR)) {

        snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
                 "%s Error: address out of tape bounds!\n", ctx->name);
        ctx->msg_ready = 1;
        ctx->mtstatectrl = MTCTRL_UARTTR;
        ctx->steptm = -1;
        break;
      }

      if (ctx->nextaddr <= FRAM_MAX_ADDR && received_steps > 0) {
        ctx->steptm = received_steps;
        ctx->stepcnt = 0;
        ctx->boundary_reached = 0;
        ctx->stop_requested = 0;
        ctx->mtstatectrl = MTCTRL_RDFRAM;
        ctx->fsmstate = FSMST_Q0;
        ResetTimer(ctx->timer_id);
      } else {
        snprintf(ctx->pending_msg, sizeof(ctx->pending_msg), "%s Error!\n",
                 ctx->name);
        ctx->msg_ready = 1;
        ctx->mtstatectrl = MTCTRL_UARTTR;
        ctx->steptm = -1;
      }
    }
    break;

  case MTCTRL_RDFRAM:
    if (ctx->boundary_reached) {
      break;
    }
    ctx->curraddr = ctx->nextaddr;
    if (ctx->curraddr > FRAM_MAX_ADDR) {
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s Error: address out of bounds\n", ctx->name);
      ctx->msg_ready = 1;
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
    int result = ProcTmStep(ctx, ctx->rdsymbol, &ctx->wrsymbol);

    if (result == -3) {
      // Обнаружено зависание - немедленно отправляем сообщение и останавливаем
      // МТ
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg), "%s>hang detected\n",
               ctx->name);
      ctx->msg_ready = 1;
      ctx->steptm = -1;
      ctx->stop_requested = 1;
      ctx->mtstatectrl = MTCTRL_UARTTR;
      break;
    } else if (result == -2) {
      ctx->boundary_reached = 1;
      if (ctx->wrsymbol != ctx->rdsymbol) {
        ctx->mtstatectrl = MTCTRL_WRFRAM;
      } else {
        ctx->mtstatectrl = MTCTRL_SYNCWAIT;
      }
      break;
    } else if (result == -1 || result == ctx->steptm) {
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s>stop addr=%03X sym='%c' %s\n", ctx->name, ctx->curraddr,
               ctx->wrsymbol, ctx->last_transition_info);
      ctx->msg_ready = 1;
      ctx->steptm = -1;
      ctx->stop_requested = 1;
      ctx->mtstatectrl = MTCTRL_UARTTR;
      break;
    } else {
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s>step=%d addr=%03X sym='%c' %s\n", ctx->name, result,
               ctx->curraddr, ctx->wrsymbol, ctx->last_transition_info);
      ctx->msg_ready = 1;
    }

    if (ctx->wrsymbol == ctx->rdsymbol) {
      ctx->mtstatectrl = MTCTRL_UARTTR;
    } else {
      ctx->mtstatectrl = MTCTRL_WRFRAM;
    }
    break;
  }

  case MTCTRL_WRFRAM:
    if (ctx->button_pending) {
      i2cFRAM_wr(ctx->button_addr, &ctx->button_symbol, 1);
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s>fail addr %d (K pressed)\n", ctx->name, ctx->button_addr);
      ctx->msg_ready = 1;
      ctx->button_pending = 0;
      ctx->mtstatectrl = MTCTRL_UARTTR;
      break;
    }

    if (ctx->curraddr <= FRAM_MAX_ADDR) {
      i2cFRAM_wr(ctx->curraddr, &ctx->wrsymbol, 1);
    }

    if (ctx->boundary_reached) {
      ctx->mtstatectrl = MTCTRL_SYNCWAIT;
    } else {
      ctx->mtstatectrl = MTCTRL_UARTTR;
    }
    break;

  case MTCTRL_UARTTR:
    if (GetTimer(ctx->timer_id) > 250) {
      ResetTimer(ctx->timer_id);
      if (ctx->msg_ready) {
        if (uart_transmit((uint8_t *)ctx->pending_msg,
                          (uint8_t)strlen(ctx->pending_msg)) == 0) {
          ctx->msg_ready = 0;
        }
      }

      // Если запрошена остановка, переходим в RXCMD
      if (ctx->stop_requested) {
        ctx->mtstatectrl = MTCTRL_RXCMD;
        ctx->boundary_reached = 0;
        ctx->stop_requested = 0;
      } else if (ctx->steptm == -1) {
        ctx->mtstatectrl = MTCTRL_RXCMD;
        ctx->boundary_reached = 0;
      } else {
        if (!ctx->boundary_reached) {
          ctx->mtstatectrl = MTCTRL_RDFRAM;
        }
      }
    }
    break;

  case MTCTRL_SYNCWAIT:
    if (!ctx->msg_ready) {
      snprintf(ctx->pending_msg, sizeof(ctx->pending_msg),
               "%s>waiting for sync\n", ctx->name);
      ctx->msg_ready = 1;
    }
    ctx->mtstatectrl = MTCTRL_UARTTR;
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
  mt1.boundary_reached = 0;
  mt1.button_pending = 0;
  mt1.msg_ready = 0;
  mt1.stop_requested = 0;
  memset(mt1.pending_msg, 0, sizeof(mt1.pending_msg));
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
  mt2.boundary_reached = 0;
  mt2.button_pending = 0;
  mt2.msg_ready = 0;
  mt2.stop_requested = 0;
  memset(mt2.pending_msg, 0, sizeof(mt2.pending_msg));
  strcpy(mt2.name, "TM2");
}

char are_both_mt_stopped(void) {
  return (mt1.mtstatectrl == MTCTRL_RXCMD && mt2.mtstatectrl == MTCTRL_RXCMD);
}