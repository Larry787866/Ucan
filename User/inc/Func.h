#ifndef FUNC_H
#define FUNC_H

#include "gd32f30x.h"


typedef enum
{
    lED_IDLE = 0X00,
    LED_Progress = 0X01,
    LED_Complete = 0X02,
} LED_State;

void led_water(void); 
void led_toggle(uint32_t port, uint32_t pin);
void led_state(LED_State state);
void led_progress(void);
void led_complete(void);

#endif /* FUNC_H */
