#include "bsp_usbd.h"
#include "bsp_gpio.h"
#include "systick.h"
#include "cdc_acm_core.h"

#include <stdio.h>
#include <string.h>

/* USB 设备对象只在本文件可见，外部一律走 bsp_usbd_* 接口 */
static usb_dev cdc_acm;

/* OUT 端点是否已武装。重复调用 cdc_acm_data_receive() 会冲掉正在收的包，
   所以用这个标志保证一包只武装一次 */
static uint8_t rx_armed = 0U;

void bsp_usbd_init(void)
{
    /* USB 48MHz 时钟：120MHz / 2.5 = 48MHz */
    rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);
    rcu_periph_clock_enable(RCU_USBD);

    /* 先断开 DP 上拉，让电脑看到一次完整的重新枚举 */
    printf("[USB] Resetting USB DP pull-up...\r\n");
    usb_soft_connect(0);
    delay_1ms(200);

    /* 初始化 USB 核心与 CDC 类，句柄在这里被存进 usbd_core.dev */
    usbd_init(&cdc_acm, &cdc_desc, &cdc_class);

    /* 使能 USBD 全局中断 */
    nvic_irq_enable(USBD_LP_CAN0_RX0_IRQn, 1, 0);

    rx_armed = 0U;

    /* DP 上拉接通，触发电脑开始枚举 */
    usb_soft_connect(1);
    printf("[USB] USB CDC Initialized, Waiting for Host...\r\n");
}

uint8_t bsp_usbd_is_ready(void)
{
    return (USBD_CONFIGURED == cdc_acm.cur_status) ? 1U : 0U;
}

void bsp_usbd_poll(void)
{
    if (0U == bsp_usbd_is_ready()) {
        rx_armed = 0U;      /* 掉线了，等重新枚举后再武装 */
        return;
    }

    if (0U == rx_armed) {
        /* cdc_acm_init() 不会自己武装 OUT 端点，这里补上第一次 */
        cdc_acm_data_receive(&cdc_acm);
        rx_armed = 1U;
    }
}

uint16_t bsp_usbd_recv(uint8_t *buf, uint16_t max_len)
{
    usb_cdc_handler *cdc;
    uint16_t n;

    if ((NULL == buf) || (0U == max_len) || (0U == bsp_usbd_is_ready())) {
        return 0U;
    }

    cdc = (usb_cdc_handler *)cdc_acm.class_data[CDC_COM_INTERFACE];
    if ((NULL == cdc) || (0U == cdc->packet_receive)) {
        return 0U;      /* 还没有收到完整的一包 */
    }

    n = (uint16_t)cdc->receive_length;
    if (n > max_len) {
        n = max_len;
    }
    memcpy(buf, cdc->data, n);

    /* 这一包已取走，重新武装 OUT 端点准备收下一包 */
    cdc_acm_data_receive(&cdc_acm);
    rx_armed = 1U;

    return n;
}

void bsp_usbd_send(uint8_t *data, uint16_t len)
{
    usb_cdc_handler *cdc;

    if ((NULL == data) || (0U == len) || (0U == bsp_usbd_is_ready())) {
        return;
    }

    cdc = (usb_cdc_handler *)cdc_acm.class_data[CDC_COM_INTERFACE];
    if (NULL == cdc) {
        return;
    }

    /* packet_sent 为 1 表示上一包已发完，可以再发 */
    if (1U == cdc->packet_sent) {
        cdc->packet_sent = 0U;
        usbd_ep_send(&cdc_acm, CDC_IN_EP, data, len);
    }
}
