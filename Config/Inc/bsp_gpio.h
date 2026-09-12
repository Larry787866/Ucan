#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include "gd32f30x.h"

/* 初始化 PA4(LED1)、PA5(LED2)、PA10(USB开关) */
void bsp_gpio_init(void);

/* LED 控制 */
void led1_toggle(void);
void led2_toggle(void);

/* USB DP 1.5k 上拉软开关控制 (0: 断开, 1: 连接) */
void usb_soft_connect(uint8_t enable);

#endif /* BSP_GPIO_H */
