/*!
    \file    main.c
    \brief   GD32F303CBT6 USB-CAN Custom Board Main Function
*/

#include "gd32f30x.h"
#include "systick.h"
#include <stdio.h>
#include "bsp_gpio.h"
#include "bsp_usart1.h"
#include "bsp_can.h"

int main(void)
{
    can_receive_message_struct rx_msg;
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint32_t timer_count = 0;
    uint8_t i = 0;

    /* 1. USB 48MHz 时钟分频 (120MHz / 2.5 = 48MHz) */
    rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);
    rcu_periph_clock_enable(RCU_USBD);

    /* 2. 滴答定时器延时初始化 */
    systick_config();

    /* 3. 板载硬件外设初始化 */
    bsp_gpio_init();         // 初始化 PA4(LED1)、PA5(LED2)、PA10(USB开关)
    bsp_usart1_init(115200); // 初始化 PA2/PA3 调试串口
    bsp_can0_init();         // 初始化 PB8/PB9 CAN0 (1 Mbps)

    /* 4. 打印时钟自检信息 */
    printf("\r\n===================================================");
    printf("\r\n   GD32F303CBT6 USB-CAN Board Online!");
    printf("\r\n   CK_SYS  is %d Hz (Target: 120MHz)", rcu_clock_freq_get(CK_SYS));
    printf("\r\n   CK_AHB  is %d Hz (Target: 120MHz)", rcu_clock_freq_get(CK_AHB));
    printf("\r\n   CK_APB1 is %d Hz (Target:  60MHz)", rcu_clock_freq_get(CK_APB1));
    printf("\r\n   CK_APB2 is %d Hz (Target: 120MHz)", rcu_clock_freq_get(CK_APB2));
    printf("\r\n===================================================\r\n");

    /* 5. USB 软插入时序控制 */
    Usb_Start();

    /* 6. 主循环 */
    while (1)
    {
        /* A. 周期任务：每 1 秒执行一次 */
        if (timer_count >= 1000)
        {
            timer_count = 0;
            led1_toggle(); // PA4 (LED1) 翻转心跳

            // 发送 CAN 测试心跳包
            can0_send_msg(0x200, test_data, 8);
            printf("[CAN TX] Sent ID: 0x200\r\n");
        }

        /* B. 实时监听 CAN 总线接收 */
        if (can0_recv_msg(&rx_msg))
        {
            led2_toggle(); // PA5 (LED2) 翻转

            printf("[CAN RX] ID:0x%03X, Data: ", rx_msg.rx_sfid);
            for (i = 0; i < rx_msg.rx_dlen; i++) {
                printf("%02X ", rx_msg.rx_data[i]);
            }
            printf("\r\n");
        }

        delay_1ms(10);
        timer_count += 10;
    }
}
