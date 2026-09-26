/*!
    \file    Protocal.h
    \brief   USB(CDC) <-> CAN0 转发协议（Ucan）—— v2

    \version 2026-9-26

    帧格式和握手说明见 Protocal.c 的文件头（也抄了一份在 README 第三节）。
*/

#ifndef PROTOCAL_H
#define PROTOCAL_H

#include "gd32f30x.h"

/* 数据传输状态（给 LED 指示用） */
#define PROTO_IDLE      0U      /* 没有数据传输 */
#define PROTO_BUSY      1U      /* 正在传 */
#define PROTO_DONE      2U      /* 刚传完 */

/* 自动测试发送（调试用）—— main.c 不用动，改完重新编译即可：
   0 = 关闭
   1 = 按下面周期自动发一帧到 VOFA（直接灌上行队列，不经过 CAN）
   2 = 按下面周期自动发一帧到 CAN 总线（用来数 Cangaroo 里出现几次） */
#define PROTO_AUTO_TEST        0U
#define PROTO_AUTO_TEST_MS     1000U

/* CAN 接收自测（调试用）：主循环里调用，每收到一帧 can_rx_count 就 +1。
   在 Keil 的 Watch 窗口里加上 can_rx_count / can_rx_last_id 就能看。
   测完把这行调用删掉、这两个变量留着也不影响。 */
void can_rx_test(void);

extern volatile uint32_t can_rx_count;
extern volatile uint32_t can_rx_last_id;

/* 链路状态：1 = 已经握手，CAN -> USB 正常上报；0 = 还没握手，CAN 帧直接丢。
   上电 / USB 重新枚举后是 0，收到握手请求（AA 55 30 01）或者任何合法数据帧
   之后变 1。在 Keil 的 Watch 窗口里加上这个变量，就能一眼看出上位机握手没有。 */
extern volatile uint8_t proto_link_ready;

void protocol_init(void);

void protocol_task(void);

/* 当前数据传输状态，用来点灯 */
uint8_t protocol_activity(void);

#endif /* PROTOCAL_H */
