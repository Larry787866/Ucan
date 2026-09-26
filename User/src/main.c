/*!
    \file    main.c
    \brief   USB CDC ACM device

    \version 2026-2-6, V3.0.3, firmware for GD32F30x
*/

/*
    Copyright (c) 2025, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "cdc_acm_core.h"
#include "drv_usb_hw.h"
#include "systick.h"
#include "Func.h"
#include "can.h"
#include "gpio.h"
#include "Protocal.h"
#include "gsusb.h"

/* USB 设备核心实例。
   ⚠ 这个是**两个模式共用**的：CDC 和 gs_usb 各有各的描述符和类，但都用这一个
     实例（同一时刻只装一个类），所以 gd32f30x_it.c 里的 usbd_isr(&cdc_acm)
     一行都不用改（中断服务是类无关的）。 */
usb_core_driver cdc_acm;

static void app_run_gsusb(void);
static void app_run_cdc(void);

/*!
    \brief      main routine
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    /* system clocks configuration */
    usb_rcu_config();

    /* USB timer initialization */
    usb_timer_init();

    /* GPIO configuration */
    led_gpio_config();

    /* enable systick */
    systick_config();

    /* CAN configuration：1 Mbps 起步，gs_usb 模式下主机会按它要的比特率覆盖 */
    can0_config();

    /* ← 用哪个模式就把另一个注释掉。
         gs_usb ：Cangaroo / Linux candump 直接认设备（通用 USB-CAN 适配器）
         cdc    ：CDC 虚拟串口 + 自己定的帧协议（用 VOFA+ 之类看字节流）*/
    app_run_gsusb();
    /* app_run_cdc(); */

    return 0;
}

/*!
    \brief      gs_usb（candleLight）模式的主循环
    \note       枚举成 USB vendor 类，VID/PID = 0x1D50 / 0x606F。
                Windows 靠 WCID 免驱（设备管理器里是 WinUsb Device），
                Linux 内核自带 gs_usb 驱动，插上就是 can0。
*/
static void app_run_gsusb(void)
{
    usbd_init(&cdc_acm, &gsusb_desc, &gsusb_class);

    /* USB interrupt configuration：
       ⚠ 要放在 usbd_init() 之后 —— usbd_init 里才会给 class_core 赋值，
         中断抢在它前面进来的话，ISR 会解引用空的 class_core。 */
    usb_intr_config();

    /* enabled USB pull-up */
    usbd_connect(&cdc_acm);

    while (USBD_CONFIGURED != cdc_acm.dev.cur_status)
    {
        /* wait for standard USB enumeration is finished */
    }

    gsusb_init();

    while (1)
    {
        gsusb_task();
    }
}

/*!
    \brief      CDC 虚拟串口模式的主循环
    \note       帧协议见 Protocal.c / README 第三节。
*/
static void app_run_cdc(void)
{
    uint32_t st;

    usbd_init(&cdc_acm, &cdc_desc, &cdc_class);

    /* USB interrupt configuration（同样要放在 usbd_init() 之后，理由见上） */
    usb_intr_config();

    /* enabled USB pull-up */
    usbd_connect(&cdc_acm);

    /* protocol initialization */
    protocol_init();

    while (USBD_CONFIGURED != cdc_acm.dev.cur_status)
    {
        /* wait for standard USB enumeration is finished */
    }

    while (1)
    {
        protocol_task();
        st = protocol_activity();
        if (PROTO_BUSY == st)
        {
            led_progress(); /* 传输中：两两交替闪 */
        }
        else if (PROTO_DONE == st)
        {
            led_complete(); /* 传输完成：四个一起闪 */
        }
        else
        {
            led_water(); /* 空闲：走马灯 */
        }
    }
}
