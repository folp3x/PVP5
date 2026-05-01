/*fsm_tm.h*/
#ifndef FSMTM_H
#define FSMTM_H

#include "main.h"

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
  char stop_requested;
  char sync_waiting;
};

extern void ProcessFsmTm(void);

extern void ProcessOneTm(struct TM_Context *ctx); 
extern void InitTM(void);

#endif