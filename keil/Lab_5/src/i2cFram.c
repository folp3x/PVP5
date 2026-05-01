/* i2cFram.c */
#include "i2cFram.h"
#include "main.h"
#include "messages.h"
#define SLAVE_OWN_ADDRESS 0xA0 	// Адресс F-RAM согласно тех. описанию FM24CL16B

typedef enum  {
	idle_ST = 0,  // состояние бездействия
	wr_ST = 1,    // состояние записи в FRAM
	rd_ST = 2,    // состояние чтения FRAM
	wait_ST = 3   // состояние ожидания чтения результата
} FRAMstateI2C; 
FRAMstateI2C iic_st; // переменная состояния

typedef struct DATI2C {
	uint8_t aBuf[IICBUFSZ+1];// Буффер приёма/передачи i2c
	uint8_t index;	    // Счётчик принятых/переданных байт
	uint8_t len;	    // Количество передаваемых байт
} DATI2C;
static struct DATI2C tDati2c;
unsigned char leni2c = 0;
// размер FRAM 2K = 2048b, max addr = 0x7FF
static uint32_t addrdat_FRAM=0; // Начальный адрес обращения к памяти F-RAM
	
void init_I2C_FRAM(void);		// Инициализация интерфейса I2C связи с памятью F-RAM
void* i2cFRAM_wr(uint32_t addrdat, uint8_t* pBuffer, uint32_t ubNumByteToWrite); //Запись пакета данных в память F-RAM по интерфейсу I2C
void* i2cFRAM_rd(uint32_t addrdat, uint32_t ubNumByteToRead); //Чтение пакета данных из памяти F-RAM по интерфейсу I2C

void init_I2C_FRAM(void) {
	iic_st = idle_ST;
	/**** I2C1 приём и передача ****/
	LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
	// GPIO PB6->I2C1_SCL ; PB7->I2C1_SDA
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
	GPIO_InitStruct.Pin = LL_GPIO_PIN_6|LL_GPIO_PIN_7;
	GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
	GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
	GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
	GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
	LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1); //разрешаем тактирование I2C1
	LL_RCC_SetI2CClockSource(LL_RCC_I2C1_CLKSOURCE_SYSCLK);//источник тактирования SYSCLK

	NVIC_SetPriority(I2C1_EV_IRQn, 0); 
	NVIC_EnableIRQ(I2C1_EV_IRQn); // разрешаем прерывание событиям I2C

	LL_I2C_SetTiming(I2C1, 0xE010DFFE); // 10KHz, Standard mode, sysclk=72MHz, RT=50ns, FT=3ns    
	LL_I2C_Enable(I2C1); // включить модуль

	LL_I2C_EnableIT_RX(I2C1); // разрешаем прерывание по приёму
	LL_I2C_EnableIT_TX(I2C1); // разрешаем прерывание по передаче
	LL_I2C_EnableIT_TC(I2C1); // разрешаем прерывание по окончанию передачи
	LL_I2C_EnableIT_STOP(I2C1); // разрешаем прерывание по окончанию обмена
	tDati2c.index = 0;
	tDati2c.len = 0;
	printf("\nIIC init\n");
}

void* i2cFRAM_wr(uint32_t addrdat, uint8_t* pBuffer, uint32_t ubNumByteToWrite) 
{
	if(iic_st == idle_ST)
	{
		if((ubNumByteToWrite <= IICBUFSZ) && (addrdat <= 0x7FF) && (ubNumByteToWrite > 0))
		{
			if(!LL_I2C_IsActiveFlag_BUSY(I2C1))
			{
				iic_st = wr_ST;
				addrdat_FRAM = addrdat;
				tDati2c.index = 0;	// обнуляем счётчик
				tDati2c.len = ubNumByteToWrite + 1;  // количество передаваемых байт
				tDati2c.aBuf[0] = (uint8_t)addrdat_FRAM ; // первый байт=адрес[7..0] в FRAM, далее записываемые данные 
				memcpy((void*)&tDati2c.aBuf[1],(void*)pBuffer,ubNumByteToWrite); // копируем передаваемые данные в буффер передачи
				// Начало обмена, передаём на шину адрес приёмника, далее передаём 1 байт адреса памяти и байты данных
				LL_I2C_HandleTransfer(
					I2C1, SLAVE_OWN_ADDRESS|(addrdat_FRAM>>7),// Адрес приёмника и старшие 3 бита адрес[10..8]  в FRAM
					LL_I2C_ADDRSLAVE_7BIT, 	// Формат адреса приёмника 7 бит
					tDati2c.len, 	//  NBYTES[7:0]=(1+ubNumByteToWrite), количество передаваемых байт 
					LL_I2C_MODE_AUTOEND, // AUTOEND=1, по окончании передачи NBYTES сформировать признак STOP
					LL_I2C_GENERATE_START_WRITE);//сгенерировать признак START и признак записи в приёмник(LSB=0)
			}
		}
	}
	else if(iic_st == wait_ST)
	{
		iic_st = idle_ST;
		return (void*)&tDati2c.aBuf;
	}
	return (void*)0;
}

void* i2cFRAM_rd(uint32_t addrdat, uint32_t ubNumByteToRead)
{
	if(iic_st == idle_ST)
	{
		if((addrdat <= 0x7FF) && (ubNumByteToRead <= IICBUFSZ) && (ubNumByteToRead > 0))
		{
			if(!LL_I2C_IsActiveFlag_BUSY(I2C1))
			{
				iic_st = rd_ST;
				addrdat_FRAM = addrdat;
				tDati2c.len = ubNumByteToRead;  // количество принимаемых байт
				I2C1->CR1 |= I2C_CR1_SWRST; // сбрасываем модуль
				tDati2c.index = 0;	// обнуляем счётчик
				tDati2c.aBuf[0] = (uint8_t)addrdat_FRAM ; // первый байт=адрес[7..0] в FRAM, далее записываемые данные 
				// Начало обмена:передаём адрес приёмника, 1 байт (адрес в памяти),признак записи в приёмник(LSB=0),признак STOP не выставляем
				LL_I2C_HandleTransfer(I2C1, SLAVE_OWN_ADDRESS,LL_I2C_ADDRSLAVE_7BIT,1,LL_I2C_MODE_SOFTEND,LL_I2C_GENERATE_START_WRITE); 
			}
		}
	}
	else if(iic_st == wait_ST)
	{
		iic_st = idle_ST;
		return (void*)&tDati2c.aBuf;
	}
	return (void*)0;
}

void I2C1_EV_IRQHandler(void)
{
	if(LL_I2C_IsActiveFlag_TXIS(I2C1)) 
	{ // если передача, то передаём следующий байт
		LL_I2C_TransmitData8(I2C1,tDati2c.aBuf[tDati2c.index]);
        tDati2c.index++;
	}
	else if(LL_I2C_IsActiveFlag_RXNE(I2C1))
	{ // если приём то принимаем байт
		tDati2c.aBuf[tDati2c.index] = LL_I2C_ReceiveData8(I2C1);
        tDati2c.index++;
	}
	else if(LL_I2C_IsActiveFlag_TC(I2C1))
	{ // Продолжение обмена при приёме: передаём адрес приёмника и старшие 3 бита адреса[10..8]   
		tDati2c.index=0;
		LL_I2C_HandleTransfer(I2C1, SLAVE_OWN_ADDRESS|(addrdat_FRAM>>7&0xE),LL_I2C_ADDRSLAVE_7BIT,
			tDati2c.len, //  количество принимаемых байт 
			LL_I2C_MODE_AUTOEND, // AUTOEND=1, по окончании передачи сформировать признак STOP
			LL_I2C_GENERATE_START_READ);// сгенерировать признак START и признак чтения из приёмника(LSB=1)
			__NOP(); __NOP(); __NOP(); __NOP(); // задержка
			__NOP(); __NOP(); __NOP(); __NOP();
	}
	else if(LL_I2C_IsActiveFlag_STOP(I2C1))
	{ // если конец приёма/передачи проверяем что счётчик совпадает с размером 
		LL_I2C_ClearFlag_STOP(I2C1);
		if(tDati2c.index != tDati2c.len)
		{ // переданные/принятые данные не совпали с заданным размером
			printf("IIC dat len Err\n");
			return;
		}
		if(iic_st == rd_ST)
			leni2c = tDati2c.len;
		iic_st = wait_ST;
	}
}
