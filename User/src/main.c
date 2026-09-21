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

usb_core_driver cdc_acm;

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

    /* USB device configuration */
    usbd_init(&cdc_acm, &cdc_desc, &cdc_class);

    /* USB interrupt configuration */
    usb_intr_config();

     /* enable systick */
    systick_config();

    /* enabled USB pull-up */
    usbd_connect(&cdc_acm);

    /* CAN configuration */
    can0_config();

    while (USBD_CONFIGURED != cdc_acm.dev.cur_status)
    {
        /* wait for standard USB enumeration is finished */
    }

    while (1)
    {
        led_water();
        //        led_state(lED_IDLE);
        //        can0_send_test();
        if (0U == cdc_acm_check_ready(&cdc_acm))
        {
            cdc_acm_data_receive(&cdc_acm);
        }
        else
        {
            cdc_acm_data_send(&cdc_acm);
        }
    }
}
