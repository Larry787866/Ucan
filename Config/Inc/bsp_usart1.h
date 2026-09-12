#ifndef BSP_USART1_H
#define BSP_USART1_H

#include "gd32f30x.h"
#include <stdio.h>

/* 初始化 PA2(TX) 和 PA3(RX) 串口 */
void bsp_usart1_init(uint32_t baudrate);
/* USB Start Function */
void Usb_Start(void);

#endif /* BSP_USART1_H */
