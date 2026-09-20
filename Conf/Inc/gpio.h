#ifndef GPIO_H
#define GPIO_H

#include "gd32f30x.h"

#define Led_GPIO_PORT    GPIOA
#define Led_GPIO_CLK     RCU_GPIOA 
#define Led_GPIO_PIN1     GPIO_PIN_4
#define Led_GPIO_PIN2     GPIO_PIN_5

void led_gpio_config(void);

#endif /* GPIO_H */
