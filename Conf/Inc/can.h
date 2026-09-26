#ifndef CAN_H
#define CAN_H

#include "gd32f30x.h"

void can0_config(void);

/* 关掉 CAN0：进初始化模式，不再收发（gs_usb 的 MODE RESET 用）。
   注意不用 can_deinit()——那是外设复位，会连验收滤波器一起清掉，
   再开就要重新配滤波器。 */
void can0_stop(void);

/* 重新配比特率和模式（gs_usb 的 BITTIMING + MODE 用）。
   brp / tseg1 / tseg2 / sjw 是**寄存器字段值**，不是时间量：
     tseg1 = prop_seg + phase_seg1 - 1，取值 0..15
     tseg2 = phase_seg2 - 1，           取值 0..7
     sjw   = sjw - 1，                  取值 0..3
     brp   = 波特率预分频，             取值 1..1024（库内部会 -1）
   listen_only / loopback / one_shot 都是 0 或 1；listen_only + loopback 组合
   对应 CAN_SILENT_LOOPBACK_MODE。
   can_init() 只写 CAN_CTL / CAN_BT，不碰滤波寄存器，所以 can0_config() 里配的
   全通滤波器重配后照样有效。
   参数越界返回 ERROR，否则返回 can_init() 的结果。 */
ErrStatus can0_reconfigure(uint32_t brp, uint32_t tseg1, uint32_t tseg2, uint32_t sjw,
                           uint8_t listen_only, uint8_t loopback, uint8_t one_shot);

/* 发一帧，带远程帧支持：is_remote != 0 发远程帧，此时 data 可以是 NULL。 */
uint8_t can0_send_frame_ex(uint32_t id, uint8_t is_extended, uint8_t is_remote,
                           uint8_t *data, uint8_t send_len);

/* 发一帧数据帧：is_extended = 1 扩展帧, 0 标准帧。不阻塞：报文投进邮箱就返回。
   send_len 允许为 0（DLC = 0 的空帧），此时不碰 data。
   返回 CAN_TRANSMIT_OK      = 已投递（注意：不等于已上总线、已被 ACK）
        CAN_TRANSMIT_NOMAILBOX = 三个邮箱都占着，这帧丢了 */
uint8_t can0_send_frame(uint32_t id, uint8_t is_extended, uint8_t *data, uint8_t send_len);

/* 发一帧标准帧 */
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t send_len);

/* 从 FIFO0 取一帧：1 = 取到了，0 = 没有新报文 */
uint8_t can0_recv_msg(can_receive_message_struct *rx_msg);

uint8_t can0_send_test(void);

#endif /* CAN_H */
