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

/* ---------------- 发送队列 ----------------
   usbd_ep_send() 只对 64 字节以内的第一包做同步拷贝，一包发完之前
   cdc->packet_sent 一直是 0。中间进来的帧排在这个队列里，避免直接丢掉。

   并发：push（bsp_usbd_send）只在主循环；pop（bsp_usbd_tx_pump）既可能在
   主循环、也可能在 USBD 中断里，所以 head/tail/busy 都是 volatile。
   tail 只有在硬件确认发完之后才前进，因此 in-flight 那一槽不会被覆盖。 */
static uint8_t  tx_queue[USBD_TX_FIFO_DEPTH][USBD_TX_FRAME_LEN];
static uint16_t tx_queue_len[USBD_TX_FIFO_DEPTH];
static volatile uint8_t  tx_head = 0U;
static volatile uint8_t  tx_tail = 0U;
static volatile uint8_t  tx_busy = 0U;
static uint32_t tx_drop = 0U;

/* pump 的重入保护。中断只能打断主循环，主循环打断不了中断，
   所以中断里发现主循环正在 pump 时直接返回就行，不需要自旋等待 */
static volatile uint8_t tx_pump_busy = 0U;

void bsp_usbd_tx_pump(void)
{
    usb_cdc_handler *cdc;

    if (0U != tx_pump_busy) {
        return;         /* 另一个上下文正在 pump，让它做完 */
    }
    tx_pump_busy = 1U;

    if (0U == bsp_usbd_is_ready()) {
        tx_busy = 0U;
        tx_pump_busy = 0U;
        return;
    }

    cdc = (usb_cdc_handler *)cdc_acm.class_data[CDC_COM_INTERFACE];
    if (NULL == cdc) {
        tx_pump_busy = 0U;
        return;
    }

    if (0U != tx_busy) {
        /* packet_sent 由 cdc_acm_data_in() 在中断里置回 1，表示上一包发完了 */
        if (1U != cdc->packet_sent) {
            tx_pump_busy = 0U;
            return;     /* 还在发，等下一次中断或轮询 */
        }
        /* 回收这个槽位 */
        tx_tail = (uint8_t)((tx_tail + 1U) % USBD_TX_FIFO_DEPTH);
        tx_busy = 0U;
    }

    if (tx_head == tx_tail) {
        tx_pump_busy = 0U;
        return;         /* 队列空 */
    }

    cdc->packet_sent = 0U;
    usbd_ep_send(&cdc_acm, CDC_IN_EP, tx_queue[tx_tail], tx_queue_len[tx_tail]);
    tx_busy = 1U;

    tx_pump_busy = 0U;
}

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

    rx_armed      = 0U;
    tx_busy       = 0U;
    tx_pump_busy  = 0U;

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
        tx_busy  = 0U;
        /* 顺手清空队列：掉线期间攒下的帧对重新接入的电脑来说已经是过期数据，
           留着会在重新枚举后被当成新帧发出去。is_ready() 为假时没人往里写，
           这里清是安全的 */
        tx_tail  = tx_head;
        return;
    }

    if (0U == rx_armed) {
        /* cdc_acm_init() 不会自己武装 OUT 端点，这里补上第一次 */
        cdc_acm_data_receive(&cdc_acm);
        rx_armed = 1U;
    }

    /* 中断没来的时候靠这里兜底，保证队列不会被卡住 */
    bsp_usbd_tx_pump();
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
    uint8_t next;

    if ((NULL == data) || (0U == len) || (0U == bsp_usbd_is_ready())) {
        return;
    }

    if (len > USBD_TX_FRAME_LEN) {
        len = USBD_TX_FRAME_LEN;    /* 超长帧截断，防止越界写 */
    }

    next = (uint8_t)((tx_head + 1U) % USBD_TX_FIFO_DEPTH);
    if (next == tx_tail) {
        tx_drop++;      /* 队列满，整帧丢弃，不会发出半截帧 */
        return;
    }

    memcpy(tx_queue[tx_head], data, len);
    tx_queue_len[tx_head] = len;
    __DMB();            /* 帧内容先于 head 对中断可见，否则中断可能读到半截帧 */
    tx_head = next;

    bsp_usbd_tx_pump();     /* 空闲的话立刻发出去，省一轮延迟 */
}

uint32_t bsp_usbd_tx_drop_count(void)
{
    return tx_drop;
}
