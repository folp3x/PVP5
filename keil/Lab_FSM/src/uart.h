/* uart.h */
#ifndef UART_H
#define UART_H

extern unsigned char lenuart;	// Длинна принятого пакета UART
void InitUartDMA(void);		// Инициализация UART и каналов DMA
// Функция запуска передачи пакета по UART с использование DMA
// Если канал занят или ошибка, то возвращает 1, если свободен 0
unsigned char uart_transmit(unsigned char* pBuffer, unsigned char ublenBuff); 

#endif
