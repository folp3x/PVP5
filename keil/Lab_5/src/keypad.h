/*keypad.h*/
#ifndef KEYPAD_H
#define KEYPAD_H

#define DEBOUNCE	10	// задержка на дребезг 10 мс
#define FIRST_DELAY	500	// задержка первого удержания кнопки 500 мс
#define AUTO_REPEAT	300	// задержка повторного удержания 300 мс

void InitKpd4(void);
void ProcessKeyKpd4(void);

#endif
