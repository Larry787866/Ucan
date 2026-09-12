#ifndef BSP_CAN_H
#define BSP_CAN_H

#include "gd32f30x.h"

/* 软件接收 FIFO 深度（帧）。硬件 FIFO 只有 3 级，靠这个缓冲突发 */
#define CAN_RX_FIFO_DEPTH   16U

/* 基础初始化：PB8(RX)、PB9(TX)，波特率 1 Mbps
   接收走 CAN0 RX1 中断 + 软件帧 FIFO，不再轮询硬件 FIFO */
void bsp_can0_init(void);

/* CAN0 RX1 中断服务程序，由 gd32f30x_it.c 的 CAN0_RX1_IRQHandler 调用 */
void bsp_can0_rx_isr(void);

/* 发送标准帧（ID: 11位, 数据: 最多8字节）
   返回 0/1/2 = 占用的邮箱号（成功），返回 3 = CAN_NOMAILBOX（三个邮箱都满，失败）
   注意 0 是成功值，别写成 if (can0_send_msg(...)) 判断失败 */
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t len);

/* 发送一帧，is_extended 非 0 表示扩展帧（ID: 29位） */
uint8_t can0_send_frame(uint32_t id, uint8_t is_extended, uint8_t *data, uint8_t len);

/* 从软件 FIFO 取一帧：收到返回 1，FIFO 空返回 0 */
uint8_t can0_recv_msg(can_receive_message_struct *rx_msg);

/* 因软件 FIFO 满而丢掉的帧数 */
uint32_t can0_rx_drop_count(void);

/* 把一帧报文打印到调试串口 */
void can0_print_msg(const can_receive_message_struct *rx_msg);

#endif /* BSP_CAN_H */
