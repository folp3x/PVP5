#include "messages.h"

// флаги состояния сообщения
#define F_OFF  0  // неактивно
#define F_PEND 1  // установлено, но неактивно
#define F_SET  2  // активно

static struct MSG_DATA
{
	int Msg;
	void *ParamPtr;
} Messages[MAX_MESSAGES];

void InitMessages(void)
{
	int i;
	for(i = 0; i < MAX_MESSAGES; i++)
		Messages[i].Msg = F_OFF;
}

// переводит в состояние, «установлено, но неактивно»
void SendMessage(int Msg, void *ParamPtr)
{
	Messages[Msg].Msg = F_PEND;
	Messages[Msg].ParamPtr = ParamPtr;
}

// в конце цикла сообщения переводим в состояние «активно» 
// и находимся в этом состоянии в следующем цикле программы 
// до его сброса принявшим его автоматом или в конце цикла
void ProcessMessages(void)
{
	int i;
	for(i = 0; i < MAX_MESSAGES; i++)
	{
		if(Messages[i].Msg == F_SET) 
			Messages[i].Msg = F_OFF;
		if(Messages[i].Msg == F_PEND) 
			Messages[i].Msg = F_SET;
	}
}

char GetMessage(int Msg, void *ParamPtr)
{
	if(Messages[Msg].Msg == F_SET)
	{
		*(void**)ParamPtr = Messages[Msg].ParamPtr;
		Messages[Msg].Msg = F_OFF;
		return 1;
	}
	return 0;
}
