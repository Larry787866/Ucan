/*!
    \file    gsusb.h
    \brief   USB vendor 类：gs_usb (candleLight) 协议（Ucan）

    \version 2026-9-26

    和 CDC 模式二选一，切换方法见 main.c 的 app_run_gsusb() / app_run_cdc()。
    协议细节和使用方法见 README 第十节。
*/

#ifndef GSUSB_H
#define GSUSB_H

#include "gd32f30x.h"
#include "usbd_core.h"

/* 一帧 20 字节（不实现硬件时间戳时的固定长度），小端：
       [0..3]   echo_id    主机发来的帧要原样回填；总线上收到的帧固定 0xFFFFFFFF
       [4..7]   can_id     bit31 = 扩展帧, bit30 = 远程帧, bit29 = 错误帧
       [8]      dlc        0..8
       [9]      channel    通道号，本板固定 0
       [10]     flags      CAN-FD 用，本板固定 0
       [11]     reserved
       [12..19] data
   对应 gs_usb.h 里的 struct gs_host_frame（classic_can 那一种）。 */
#define GSUSB_FRAME_LEN     20U

/* 端点包长：candleLight 用 bulk 64 字节 */
#define GSUSB_EP_SIZE       64U

/* 这两个直接交给 usbd_init() */
extern usb_desc       gsusb_desc;
extern usb_class_core gsusb_class;

/*!
    \brief      枚举完成（USBD_CONFIGURED）之后调一次，清运行态
    \note       类回调 init 里已经清过一遍，这里再清是为了防"枚举完了但
                主机还没发过任何请求"的情况（USB reset 不会调 deinit）。
*/
void gsusb_init(void);

/*!
    \brief      gs_usb 任务，主循环里每轮调用
    \note       只有这里会碰 CAN 寄存器和 usbd_ep_send()：
                主机 -> CAN、CAN -> 主机、上行发送、开/关 CAN。
*/
void gsusb_task(void);

#endif /* GSUSB_H */
