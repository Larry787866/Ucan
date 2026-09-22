/*!
    \file    Protocal.h
    \brief   USB(CDC) <-> CAN0 转发协议（Ucan）

    \version 2026-9-22
*/

#ifndef PROTOCAL_H
#define PROTOCAL_H

#include "gd32f30x.h"

/* 数据传输状态（给 LED 指示用） */
#define PROTO_IDLE      0U      /* 没有数据传输 */
#define PROTO_BUSY      1U      /* 正在传 */
#define PROTO_DONE      2U      /* 刚传完 */

void protocol_init(void);

void protocol_task(void);

/* 当前数据传输状态，用来点灯 */
uint8_t protocol_activity(void);

#endif /* PROTOCAL_H */
