#ifndef PROTOCAL_H
#define PROTOCAL_H

#include "gd32f30x.h"
#include "bsp_can.h"

/* ---- 帧格式：定长 18 字节，多字节字段一律大端 --------------------------
 *  偏移  长度  内容
 *  [0]    1   帧头 1    0xAA
 *  [1]    1   帧头 2    0x55
 *  [2]    1   帧类型    0x01 = 标准帧, 0x02 = 扩展帧
 *  [3]    4   CAN ID    高字节在前 (标准帧只用低 11 位)
 *  [7]    1   数据长度  DLC, 0~8
 *  [8]    8   数据内容, 不足 8 字节的高位补 0
 *  [16]   1   校验和    [2]~[15] 逐字节累加，取低 8 位
 *  [17]   1   帧尾      0xEE
 * ---------------------------------------------------------------------- */
#define PKT_SOF1        0xAAU
#define PKT_SOF2        0x55U
#define PKT_EOF         0xEEU
#define PKT_TOTAL_LEN   18U

/* [2] 帧类型字段的取值 */
#define PKT_TYPE_STD    0x01U
#define PKT_TYPE_EXT    0x02U

/* 解包后的一帧 (本模块内部用裸数组收发，这个结构体给上层取用) */
typedef struct {
    uint32_t id;
    uint8_t  is_extended;   /* 0: 标准帧, 1: 扩展帧 */
    uint8_t  len;           /* 数据长度 0~8 */
    uint8_t  data[8];       /* 数据内容 */
} protocol_frame_t;

void protocol_init(void);

/* 方向 1：电脑 -> CAN */
void protocol_usb_feed(uint8_t *buf, uint32_t len);
void protocol_usb_to_can_process(void);

/* 方向 2：CAN -> 电脑 */
void protocol_can_to_usb_send(can_receive_message_struct *can_rx);

#endif /* PROTOCAL_H */
