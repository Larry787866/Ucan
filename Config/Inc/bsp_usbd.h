#ifndef BSP_USBD_H
#define BSP_USBD_H

#include "gd32f30x.h"

/* USB 48MHz 时钟 + DP 软复位时序 + 协议栈初始化 + NVIC 使能 */
void bsp_usbd_init(void);

/* 主循环轮询：维护枚举状态并武装接收端点 */
void bsp_usbd_poll(void);

/* 电脑已完成枚举返回 1，否则返回 0 */
uint8_t bsp_usbd_is_ready(void);

/* 取走电脑下发的一包数据，返回字节数；返回 0 表示当前没有完整的一包 */
uint16_t bsp_usbd_recv(uint8_t *buf, uint16_t max_len);

/* 向电脑的 USB 虚拟串口发送数据（上一包未发完时丢弃，不阻塞）
   注意：data 在硬件把数据搬完前必须保持有效，单包 64 字节以内是同步拷贝的 */
void bsp_usbd_send(uint8_t *data, uint16_t len);

#endif /* BSP_USBD_H */
