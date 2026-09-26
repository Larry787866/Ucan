#ifndef CAN_H
#define CAN_H

#include "gd32f30x.h"

void can0_config(void);

/* 发一帧：is_extended = 1 扩展帧, 0 标准帧。不阻塞：报文投进邮箱就返回。
   返回 CAN_TRANSMIT_OK      = 已投递（注意：不等于已上总线、已被 ACK）
        CAN_TRANSMIT_NOMAILBOX = 三个邮箱都占着，这帧丢了 */
uint8_t can0_send_frame(uint32_t id, uint8_t is_extended, uint8_t *data, uint8_t send_len);

/* 发一帧标准帧 */
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t send_len);

/* 从 FIFO0 取一帧：1 = 取到了，0 = 没有新报文 */
uint8_t can0_recv_msg(can_receive_message_struct *rx_msg);

uint8_t can0_send_test(void);

#endif /* CAN_H */
