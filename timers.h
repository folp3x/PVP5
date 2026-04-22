/*timers.h*/
#ifndef TIMERS_h
#define TIMERS_h

#define MAX_TIMERS 5 // максимальное количество таймеров
// в этом разделе объявляются константы, служащие идентификаторами таймеров.
#define KEYB_TIMER 0 // таймер удержания кнопок
#define KPD4_TIMER 1 // период опроса состояния кнопок
#define LED_TIMER 2  // таймер переключения светодиода
#define TM1_TIMER 3  // таймер шага 1ой МТ
#define TM2_TIMER 4
// функции работы с таймерами
void InitTimers(void);
unsigned int GetTimer(char Timer);
void ResetTimer(char Timer);
#endif
