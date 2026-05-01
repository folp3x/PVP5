/*fsm_main.c*/
#include "timers.h"
#include "messages.h"
#include "fsm_main.h"
#include "uart.h"
#include "i2cFram.h"
#include "main.h"

/*
		Формат команд:
		[command] "<" [address] ":" [len] ">"  [data]
		[command] = "rd" | "wr" | "tm" 	// nhb команды чтение, запись, машина тьюринга
		[address] = [hd][hd][hd] 	// адрес памяти в диапазоне от 0 до 7FF
		[len] = [hd][hd]  // длинна цепочки байт чтения от 0 до FE , записи до F4 
		[hd] = [шестнадцатиричное число от 0 до F]
		[data] = [десятичное число от 0 до 9] | [латинская буква от A до z]
		Пример команды:
		wr<100:03>ABC - запись по адресу 0x100 трёх байт ABC
		rd<FF:04> - чтение по адресу 0xFF четырёх байт
		rd<0:1>	 - чтение по адресу 0x0 одного байта
		tm<1:7>010:01F - запуск работы первой машины тьюринга с адреса 0x102 на 0x1F шагов 
		Общая длинна пакета данный по UART не должна превышать 254 знака 
*/

/*состояния автомата*/
// ожидание пакета данных от UART
#define FSMST_UARTRX	0
// передача пакета данных по UART
#define FSMST_UARTTR	1
// чтения пакета данных из FRAM
#define FSMST_RDFRAM	2
// запись пакета данных в FRAM
#define FSMST_WRFRAM	3

int lenfsm;
static int addrfsm;
static uint8_t aRecvbuf[255];
		
static char fsmstate = FSMST_UARTRX;   // переменная состояния автомата

void ProcessFsmMain(void)
{
	int i;
	uint8_t* pRxbuf=0;
	switch (fsmstate)
	{
		case FSMST_UARTRX:
			if(GetMessage(MSG_UART_RX,(void*)&pRxbuf))
			{
				if(lenuart>6)
				{//синтаксический анализ команды
					addrfsm = -1;
					lenfsm = -1;
					sscanf((char*)&(pRxbuf[2]),"<%x:%x>",&addrfsm,&lenfsm);
					if(addrfsm>=0 && addrfsm<=0x7FF && lenfsm>0 && lenfsm<=IICBUFSZ)
					{
						if(pRxbuf[0] == 'r' && pRxbuf[1] == 'd' )
						{
							fsmstate = FSMST_RDFRAM;
							break;
						}
						else 
						{
							char* pbuf = NULL;
							pbuf = strchr ( (char*)pRxbuf, '>');
							uint8_t pos = pbuf-(char*)pRxbuf+1;
							if (pbuf != NULL && lenfsm <= (lenuart-pos) )
							{
								pbuf++;
								memcpy ( (void*)aRecvbuf, pbuf,lenfsm);
								if(pRxbuf[0] == 'w' && pRxbuf[1] == 'r')
								{
									fsmstate =  FSMST_WRFRAM;
									break;
								}
								if(pRxbuf[0] == 't' && pRxbuf[1] == 'm')
								{
									if(addrfsm == 1)
									{
										SendMessage(MSG_TM1STRT,(void*)&aRecvbuf);
										break;
									}
								}
							}
						}
					}
				}
				strcpy((char*)aRecvbuf,"Err> cmd error\n");
				lenfsm = strlen((char*)aRecvbuf);
				fsmstate = FSMST_UARTTR;
			}
		break;
		case FSMST_RDFRAM: // читаем из памяти
			pRxbuf = (uint8_t*)i2cFRAM_rd(addrfsm,lenfsm);
			if( (uint32_t)pRxbuf > 0)
			{
				// окончание чтения FRAM
				lenfsm = leni2c;
				aRecvbuf[lenfsm]='\n';
				lenfsm++;
				memcpy(aRecvbuf, pRxbuf, leni2c);
				fsmstate = FSMST_UARTTR;
			}
		break;
		case FSMST_WRFRAM: // пишем в память
			if((uint32_t)i2cFRAM_wr(addrfsm,aRecvbuf,lenfsm) > 0)
			{
				// окончание записи FRAM		
				strcpy((char*)aRecvbuf,"wr> ok\n");
				lenfsm = strlen((char*)aRecvbuf);
				fsmstate = FSMST_UARTTR;
			}
		break;
		case FSMST_UARTTR:
			if( uart_transmit(aRecvbuf,lenfsm) == 0 )
				fsmstate = FSMST_UARTRX;
		break;
	}
}
