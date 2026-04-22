/*messages.h*/
#ifndef MESSAGES_h
#define MESSAGES_h

#define MAX_MESSAGES 6
#define MAX_MSG_DATA 64

#define MSG_UART_RX 0    // сообщение о приёме пакета UART
#define MSG_TM1STRT 1    // сообщение о запуске машины тьюринга
#define MSG_KEYPRESSED 2 // сообщение о нажатии кнопок
#define MSG_TM2STRT 3
#define MSG_SYNC_MT1_TO_MT2 4 // MT1 достигла правой границы
#define MSG_SYNC_MT2_TO_MT1 5 // MT2 достигла левой границы

void ProcessMessages(void);
void InitMessages(void);
void FSM_SendMessage(int Msg, void *ParamPtr);
char FSM_GetMessage(int Msg, void *ParamPtr);
#endif
