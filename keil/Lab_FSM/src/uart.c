/* uart.c */
#include "main.h"
#include "uart.h"
#include "messages.h"

#define UARTBUFSZ 256

struct DATUART {
	uint8_t aTxDMABuf[UARTBUFSZ]; // Буффер передачи UART DMA
	uint8_t aRxDMABuf[UARTBUFSZ]; // Кольцевой буффер приёма UART DMA
	uint8_t aRxBuf[UARTBUFSZ];    // Буффер куда копируем принятый пакет по UART
	uint8_t ubRxsz;		 	      // Количество принятых байт в пакете
	uint8_t ubTxsz;			      // Количество отправляемых байт в пакете
};

uint8_t lenuart = 0;
static struct DATUART tDatuart;
//Передача пакета байт длинны ublenBuff по UART
uint8_t uart_transmit(uint8_t* pBuffer, uint8_t ublenBuff) {
	if(LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_7)!= SET) {// канал свободен?
		tDatuart.ubTxsz = ublenBuff;
		memcpy(tDatuart.aTxDMABuf, pBuffer, tDatuart.ubTxsz);
		LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_7, tDatuart.ubTxsz); //Количество передаваемых байт
		LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_7); //Включаем канал
		return 0;
	}
	return 1;
}

void InitUartDMA(void)  {
	/**** USART2 приём и передача с использование DMA1 ****/
	// Включаем тактирование ПВВ А, USART2, DMA1
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
	// Настраиваем PA2 - USART2_TX
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_2, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_2, LL_GPIO_AF_7);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_2, LL_GPIO_SPEED_FREQ_HIGH);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_2, LL_GPIO_PULL_DOWN);
	// Настраиваем PA3 - USART2_RX
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_3, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_3, LL_GPIO_AF_7);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_3, LL_GPIO_SPEED_FREQ_HIGH);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_3, LL_GPIO_PULL_UP);
	// Настройка канала№7 DMA на передачу
	LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_7, 
		LL_DMA_DIRECTION_MEMORY_TO_PERIPH | //Направление передачи из ОЗУ к ПУ
		LL_DMA_PRIORITY_HIGH              | //Приоритет канал№7 высокий
		LL_DMA_MODE_NORMAL                | //Режим счётчика адреса - линейный со сбросом к начальному адресу
		LL_DMA_PERIPH_NOINCREMENT         | //Указатель адреса в ПУ неизменен
		LL_DMA_MEMORY_INCREMENT           | //Указатель адреса в ОЗУ увеличивается после каждой передачи байта данных
		LL_DMA_PDATAALIGN_BYTE            | //Размер данных в ПУ - байт
		LL_DMA_MDATAALIGN_BYTE);			//Размер данных в ОЗУ - байт
	LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_7, 	//Указание адресов для перемещения данных
		(uint32_t)tDatuart.aTxDMABuf, 		//Начальный адрес в памяти буффера передачи
		LL_USART_DMA_GetRegAddr(USART2, LL_USART_DMA_REG_DATA_TRANSMIT),//Адрес регистра USART_TDR  
		LL_DMA_GetDataTransferDirection(DMA1, LL_DMA_CHANNEL_7));
	LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_7, 0); //Количество передаваемых байт
	// Настройка канала№6 DMA на приём
	LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_6, 
		LL_DMA_DIRECTION_PERIPH_TO_MEMORY | //Направление передачи от ПУ в ОЗУ
		LL_DMA_PRIORITY_LOW               | //Приоритет канал№6 низкий
		LL_DMA_MODE_CIRCULAR              | //Режим счётчика адреса - кольцевой
		LL_DMA_PERIPH_NOINCREMENT         | //Указатель адреса в ПУ неизменен
		LL_DMA_MEMORY_INCREMENT           | //Указатель адреса в ОЗУ увеличивается после каждого приёма байта данных
		LL_DMA_PDATAALIGN_BYTE            | //Размер данных в ПУ - байт
		LL_DMA_MDATAALIGN_BYTE);            //Размер данных в ОЗУ - байт
	LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_6,   	//Указание адресов для перемещения данных
				LL_USART_DMA_GetRegAddr(USART2, LL_USART_DMA_REG_DATA_RECEIVE),//Адрес регистра USART_RDR 
				(uint32_t)tDatuart.aRxDMABuf,			//Начальный адрес в памяти буффера приёма
				LL_DMA_GetDataTransferDirection(DMA1, LL_DMA_CHANNEL_6));
	LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_6, UARTBUFSZ);//Размер кольцевого буфера
 	LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_7); // Разрешение прерывания каналу №7 по завершению передачи
	LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_7); // Разрешение прерывания при ошибке обмена
	LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_6); // Разрешение прерывания при ошибке обмена
	/**** Настройка параметров USART2(режим UART) ****/
	// Дуплексный режим  чтения/записи TX/RX
	LL_USART_SetTransferDirection(USART2, LL_USART_DIRECTION_TX_RX);
	// Формат кадра данных 10бит = 1 старт бит, 8 бит данных, 1 стоп бит, бит чётности - отключен
	LL_USART_ConfigCharacter(USART2, LL_USART_DATAWIDTH_8B, LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
	// Передача/приём кадра данных младшими битами вперёд 
	LL_USART_SetTransferBitOrder(USART2, LL_USART_BITORDER_LSBFIRST);
	// Скорость приёма/передачи 115200, частота шины APB1 36МГц
    LL_USART_SetBaudRate(USART2, LL_RCC_GetUSARTClockFreq(LL_RCC_USART2_CLKSOURCE), LL_USART_OVERSAMPLING_16, 9600); 
    LL_USART_EnableIT_IDLE(USART2); // Разрешаем прерывание после прекращения кадров данных
	LL_USART_EnableIT_ERROR(USART2);// Разрешаем прерывание при ошибках USART2 ERROR Interrupt */
	// Разрешаем прерывания 
	NVIC_SetPriority(USART2_IRQn, 1);
	NVIC_EnableIRQ(USART2_IRQn);
	NVIC_EnableIRQ(DMA1_Channel7_IRQn);
	// Разрешаем прерывание по приёму/передаче USART2 RX/TX в DMA
    LL_USART_EnableDMAReq_RX(USART2);
	LL_USART_EnableDMAReq_TX(USART2);
	// Включаем в работу, на приём USART2, канал DMA  
	LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_6);
	LL_USART_Enable(USART2);
	tDatuart.ubTxsz = 0;
	tDatuart.ubRxsz = 0;
	printf("\nUART init\n");
	uart_transmit((uint8_t*)"\nSTM32F3D connect\n",18);
}

void USART2_IRQHandler(void) {
    static uint8_t old_pos=0;
    uint8_t pos;
    if (LL_USART_IsEnabledIT_IDLE(USART2) && LL_USART_IsActiveFlag_IDLE(USART2)) 
	{ // Прерывание простоя при приёме
        LL_USART_ClearFlag_IDLE(USART2);
		// Вычисляем позицию в буффере
		pos = UARTBUFSZ - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_6);
		if(pos!=old_pos) {
			if (pos > old_pos) { //Новая позиция выше старой
				tDatuart.ubRxsz=pos-old_pos;//копируем в буффер
				memcpy(tDatuart.aRxBuf,&tDatuart.aRxDMABuf[old_pos],tDatuart.ubRxsz);
			} 
			else { //Новая позиция ниже старой
				tDatuart.ubRxsz=UARTBUFSZ - old_pos + pos; //копируем в буффер 
				memcpy(tDatuart.aRxBuf,&tDatuart.aRxDMABuf[old_pos],UARTBUFSZ - old_pos);
				if(pos!=0)
					memcpy(&tDatuart.aRxBuf[UARTBUFSZ - old_pos],tDatuart.aRxDMABuf,pos);
			}
			old_pos = pos;//запоминаем текующую позицию
			lenuart = tDatuart.ubRxsz;
			SendMessage(MSG_UART_RX,(void*)&tDatuart.aRxBuf); // Сообщаем о принятом пакете
		}
    }
	else { // Ошибка приёма/передачи
		printf("Uart Error\n"); // Ошибка приёма/передачи
		LL_USART_ClearFlag_FE(USART2);
		LL_USART_ClearFlag_PE(USART2);
		LL_USART_ClearFlag_ORE(USART2);
	}
}

void DMA1_Channel7_IRQHandler(void) {
	if(LL_DMA_IsEnabledIT_TC(DMA1, LL_DMA_CHANNEL_7) && LL_DMA_IsActiveFlag_TC7(DMA1))
	{// Прерывание по опустошению буффера
		LL_DMA_ClearFlag_GI7(DMA1); // Передали пакет
		LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_7);// Отключили канал
	}
}
