#include "main.h"
#include "timers.h"
#include "keypad.h"
#include "fsm_main.h"
#include "fsm_tm.h"
#include "messages.h"
#include "i2cFram.h"
#include "uart.h"

void SystemClock_Config(void);
void ProcessKey(void);

int main(void)
{
	SystemClock_Config();
	SystemCoreClockUpdate();
	printf("\nSet clock=%d\n",SystemCoreClock);
	InitMessages();	//инициализация механизма обработки сообщений
	InitTimers();	//инициализация таймеров
	InitKpd4();		//инициализация автомата Kpd4
	InitUartDMA();	//инициализация UART
	init_I2C_FRAM();//инициализация IIC для работы с FRAM
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOE);
	LL_GPIO_SetPinMode(GPIOE,LL_GPIO_PIN_13, LL_GPIO_MODE_OUTPUT);
	LL_SYSTICK_EnableIT();
	while (1)
	{
		if(GetTimer(LED_TIMER)>=70) //каждые 70 мс
		{
			ResetTimer(LED_TIMER);
			LL_GPIO_TogglePin(GPIOE,LL_GPIO_PIN_13);
		}
		ProcessKey();      // итерация автомата отклика на панель кнопок
		ProcessFsmMain();  // итерация автомата работы с интерфейсами 
		ProcessFsmTm();    // итерация автомата машины тьюринга
		ProcessKeyKpd4();  // итерация автомата состояния кнопок панели
		ProcessMessages(); // обработка сообщений
	}
}

void SystemClock_Config(void)
{   // PLL (HSE) = SYSCLK = HCLK(Hz) = 72000000
	LL_FLASH_SetLatency(LL_FLASH_LATENCY_2); /* Set FLASH latency */ 
	LL_RCC_HSE_Enable(); /* Enable HSE and wait for activation*/
	while(LL_RCC_HSE_IsReady() != 1) {}
	LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE_DIV_1, LL_RCC_PLL_MUL_9);
	LL_RCC_PLL_Enable();/* Main PLL configuration and activation */
	while(LL_RCC_PLL_IsReady() != 1) {}
	LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1); //AHB Prescaler=1
	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
	while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL){}
	LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2); //APB1 Prescaler=2
	LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1); //APB2 Prescaler=1
	LL_Init1msTick(72000000);
	LL_SetSystemCoreClock(72000000);
}

// ожидание нажатия кнопки
#define FSMST_WAITKEY	0
// передача строки о зажатых кнопок по UART
#define FSMST_UARTKEY	1

void ProcessKey(void)
{
	static char fsmstate=0;   // переменная состояния автомата
	static char textbuf[17]="";
	static int count=0;
	void* ParamPtr=0;
	switch (fsmstate)
	{
		case FSMST_WAITKEY:
			if(GetMessage(MSG_KEYPRESSED,(void*)&ParamPtr))
			{
				uint16_t kypd4status=*(uint16_t*)ParamPtr; // сохранить код состояния кнопок
				int i=0;
				for(i=0;i<16;i++)
				{
					if(kypd4status & (1<<i))
					{
						textbuf[count+0] = 'K';
						textbuf[count+1] = 0x30+((i+1)/10);
						textbuf[count+2] = 0x30+((i+1)%10);
						textbuf[count+3] = ' ';
						count += 4;
					}
				}
				if(count>0)
				{
					textbuf[count++]='\n';
					fsmstate = FSMST_UARTKEY;
				}
			}
		break;
		case FSMST_UARTKEY:
			if( uart_transmit((uint8_t*)textbuf,count) == 0 )
			{
				fsmstate = FSMST_WAITKEY;
				count=0;
			}
		break;
	}
}