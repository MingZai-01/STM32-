#ifndef __BUTTON_H
#define __BUTTON_H

extern uint8_t Button_SingPrsFlag;
extern uint16_t Button_ContiPrsCount;
extern uint8_t Button_contiPrsFlag;
extern uint16_t Button_DoubPrsCount;
extern uint8_t Button_DoubPrsFlag;

void Button_Init(void);

#endif
