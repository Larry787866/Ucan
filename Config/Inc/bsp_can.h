#ifndef BSP_CAN_H
#define BSP_CAN_H

#include "gd32f30x.h"

/* 基础初始化：PB8(RX)、PB9(TX)，波特率设为 1 Mbps */
void bsp_can0_init(void);

/* 发送标准帧（ID: 11位, 数据: 最多8字节） */
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t len);

/* 接收报文检测（收到返回 1，未收到返回 0） */
uint8_t can0_recv_msg(can_receive_message_struct *rx_msg);

#endif /* BSP_CAN_H */
