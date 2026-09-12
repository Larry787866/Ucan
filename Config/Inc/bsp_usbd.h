#ifndef BSP_USBD_H
#define BSP_USBD_H

#include "gd32f30x.h"

/* 发送队列深度（帧）。硬件发完一包之前进来的数据先在这里排队 */
#define USBD_TX_FIFO_DEPTH  8U

/* 单帧最大字节数，等于 CDC IN 端点的包长 */
#define USBD_TX_FRAME_LEN   64U

/* USB 48MHz 时钟 + DP 软复位时序 + 协议栈初始化 + NVIC 使能 */
void bsp_usbd_init(void);

/* 主循环轮询：维护枚举状态、武装接收端点、推动发送队列 */
void bsp_usbd_poll(void);

/* 电脑已完成枚举返回 1，否则返回 0 */
uint8_t bsp_usbd_is_ready(void);

/* 取走电脑下发的一包数据，返回字节数；返回 0 表示当前没有完整的一包 */
uint16_t bsp_usbd_recv(uint8_t *buf, uint16_t max_len);

/* 把一帧数据排进发送队列并立刻尝试发送，不阻塞。
   队列满时整帧丢弃（不会发出半截帧），丢弃数见 bsp_usbd_tx_drop_count() */
void bsp_usbd_send(uint8_t *data, uint16_t len);

/* 把队列里的下一帧交给硬件。带重入保护，可以从主循环调用，
   也可以从 USBD 中断里调用——由 gd32f30x_it.c 的
   USBD_LP_CAN0_RX0_IRQHandler 在 usbd_isr() 之后调用，
   这样上一包发完的瞬间就能接着发下一帧，不必等主循环轮询 */
void bsp_usbd_tx_pump(void);

/* 因发送队列满而丢掉的帧数 */
uint32_t bsp_usbd_tx_drop_count(void);

#endif /* BSP_USBD_H */
