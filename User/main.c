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

/* 引入 USB CDC 核心头文件 */
#include "cdc_acm_core.h"

/* 定义 USB 设备核心对象 */
usb_dev cdc_acm;

/*!
    \brief      向电脑的 USB 虚拟串口主动发送数据
*/
void usb_cdc_send_bytes(usb_dev *udev, uint8_t *data, uint16_t len)
{
    usb_cdc_handler *cdc = (usb_cdc_handler *)udev->class_data[CDC_COM_INTERFACE];
    if (1U == cdc->packet_sent) {
        cdc->packet_sent = 0U;
        usbd_ep_send(udev, CDC_IN_EP, data, len);
    }
}

int main(void)
{
    can_receive_message_struct rx_msg;
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint32_t timer_count = 0;
    uint8_t i = 0;

    /* 1. USB 48MHz 时钟配置 (120MHz / 2.5 = 48MHz) */
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

    /* 5. 完整的 USB 软复位与协议栈初始化时序 */
    printf("[USB] Resetting USB DP pull-up...\r\n");
    usb_soft_connect(0);     // PA10 拉低，通知电脑断开
    delay_1ms(200);

    /* 初始化 USB 核心与 CDC 类 */
    usbd_init(&cdc_acm, &cdc_desc, &cdc_class);

    /* 使能 USBD 全局中断 */
    nvic_irq_enable(USBD_LP_CAN0_RX0_IRQn, 1, 0);

    /* PA10 拉高，触发电脑开始枚举 */
    usb_soft_connect(1);
    printf("[USB] USB CDC Initialized, Waiting for Host...\r\n");

    /* 6. 主循环 */
    while (1)
    {
        /* --- A. USB 虚拟串口接收/回环处理 --- */
        if (USBD_CONFIGURED == cdc_acm.cur_status)
        {
            if (0U == cdc_acm_check_ready(&cdc_acm)) {
                cdc_acm_data_receive(&cdc_acm); // 准备接收下一包
            } else {
                cdc_acm_data_send(&cdc_acm);    // 如果电脑发了数据，自动回传回显
            }
        }

        /* --- B. 周期任务：每 1 秒执行一次 --- */
        if (timer_count >= 1000)
        {
            timer_count = 0;
            led1_toggle(); // PA4 (LED1) 翻转心跳

            // 1. 发送 CAN 测试心跳包
            can0_send_msg(0x200, test_data, 8);
            printf("[CAN TX] Sent ID: 0x200\r\n");

            // 2. 通过 USB 虚拟串口向电脑发送一条提示信息
            if (USBD_CONFIGURED == cdc_acm.cur_status)
            {
                uint8_t usb_msg[] = "GD32 USB-CAN CDC Port Running!\r\n";
                usb_cdc_send_bytes(&cdc_acm, usb_msg, sizeof(usb_msg) - 1);
            }
        }

        /* --- C. 实时监听 CAN 总线接收 --- */
        if (can0_recv_msg(&rx_msg))
        {
            led2_toggle(); // PA5 (LED2) 翻转

            // 调试串口输出
            printf("[CAN RX] ID:0x%03X, Data: ", rx_msg.rx_sfid);
            for (i = 0; i < rx_msg.rx_dlen; i++) {
                printf("%02X ", rx_msg.rx_data[i]);
            }
            printf("\r\n");

            // 同步转发到电脑的 USB 虚拟串口
            if (USBD_CONFIGURED == cdc_acm.cur_status)
            {
                uint8_t usb_buf[64];
                uint16_t len = sprintf((char *)usb_buf, "[USB-CAN] RX ID:0x%03X\r\n", rx_msg.rx_sfid);
                usb_cdc_send_bytes(&cdc_acm, usb_buf, len);
            }
        }

        delay_1ms(10);
        timer_count += 10;
    }
}
