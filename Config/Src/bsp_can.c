#include "bsp_can.h"

#include <stdio.h>

/* 环回自测开关：置 1 时 CAN 控制器自己给自己 ACK，发出去的帧会原样回到接收
   FIFO，不需要总线上接第二个节点。用来单独验证 USB<->CAN 转发链路。
   接真实总线 / 板子接到车上之前必须改回 0，否则永远收不到别人的帧 */
#define BSP_CAN_TEST_LOOPBACK   0U

/* ---------------- 接收软件 FIFO ----------------
   单生产者（CAN0_RX1 中断）/ 单消费者（主循环）环形队列：
   head 只在中断里改，tail 只在主循环里改，两个都是 8 位，
   8 位读写在 Cortex-M 上是原子的，所以不需要关中断保护 */
static can_receive_message_struct can_rx_fifo[CAN_RX_FIFO_DEPTH];
static volatile uint8_t  can_rx_head = 0U;
static volatile uint8_t  can_rx_tail = 0U;
static volatile uint32_t can_rx_drop = 0U;

void bsp_can0_init(void)
{
    can_parameter_struct can_parameter;
    can_filter_parameter_struct can_filter;

    /* 1. 开启时钟：GPIOB、AFIO复用时钟、CAN0 外设时钟 */
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_CAN0);

    /* 2. 引脚配置：将 CAN0 重映射到 PB8 和 PB9 */
    gpio_pin_remap_config(GPIO_CAN_PARTIAL_REMAP, ENABLE);

    // PB9 (TX): 复用推挽输出 (AF_PP)
    gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    // PB8 (RX): 上拉输入 (IPU)
    gpio_init(GPIOB, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_8);

    /* 3. 基础控制器参数配置 */
    can_deinit(CAN0);
    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);

    can_parameter.time_triggered = DISABLE;          // 关闭时间触发模式
    can_parameter.auto_bus_off_recovery = ENABLE;    // 自动离线恢复
    can_parameter.auto_wake_up = ENABLE;             // 自动唤醒
    can_parameter.auto_retrans = ENABLE;             // 自动重传
    can_parameter.rec_fifo_overwrite = DISABLE;      // 接收 FIFO 溢出不覆盖
    can_parameter.trans_fifo_order = DISABLE;        // 发送优先级由报文 ID 决定
#if (1U == BSP_CAN_TEST_LOOPBACK)
    can_parameter.working_mode = CAN_LOOPBACK_MODE;  // 自测：自发自收，不依赖第二个节点
#else
    can_parameter.working_mode = CAN_NORMAL_MODE;    // 正常总线通信模式
#endif

    can_parameter.prescaler = 6;
    can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
    can_parameter.time_segment_1 = CAN_BT_BS1_7TQ;
    can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
    can_init(CAN0, &can_parameter);

    /* 4. 全通过滤器。注意挂在 FIFO1 上：
           FIFO0 的非空中断和 USB 共用一根向量（USBD_LP_CAN0_RX0_IRQHandler），
           挂在 FIFO1 就能用 CAN0_RX1 这根独立向量，和 USB 互不干扰 */
    can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);
    can_filter.filter_number = 0;                    // 使用第 0 组过滤器
    can_filter.filter_mode = CAN_FILTERMODE_MASK;    // 掩码屏蔽模式
    can_filter.filter_bits = CAN_FILTERBITS_32BIT;   // 32 位全宽
    can_filter.filter_list_high = 0x0000;            // 匹配 ID 设为 0
    can_filter.filter_list_low  = 0x0000;
    can_filter.filter_mask_high = 0x0000;            // 掩码设为 0（全部放行）
    can_filter.filter_mask_low  = 0x0000;
    can_filter.filter_fifo_number = CAN_FIFO1;       // 存入 FIFO1 邮箱
    can_filter.filter_enable = ENABLE;               // 激活过滤器
    can_filter_init(&can_filter);

    /* 5. 接收改成中断驱动：FIFO1 一非空就进 CAN0_RX1_IRQHandler */
    can_interrupt_enable(CAN0, CAN_INT_RFNE1);
    nvic_irq_enable(CAN0_RX1_IRQn, 1, 0);
}

/* CAN0 RX1 中断：把硬件 FIFO 里的帧尽快搬进软件 FIFO。
   硬件 FIFO 只有 3 级，不在这里立刻搬空就会丢帧 */
void bsp_can0_rx_isr(void)
{
    can_receive_message_struct frame;
    uint8_t next;

    while (0U != can_receive_message_length_get(CAN0, CAN_FIFO1)) {
        can_message_receive(CAN0, CAN_FIFO1, &frame);

        next = (uint8_t)((can_rx_head + 1U) % CAN_RX_FIFO_DEPTH);
        if (next == can_rx_tail) {
            can_rx_drop++;      /* 软件 FIFO 也满了，只能丢 */
        } else {
            can_rx_fifo[can_rx_head] = frame;
            __DMB();            /* 保证帧内容先于 head 对主循环可见 */
            can_rx_head = next;
        }
    }

    can_interrupt_flag_clear(CAN0, CAN_INT_FLAG_RFL1);
}

/* 发送一帧数据，可指定标准帧或扩展帧 */
uint8_t can0_send_frame(uint32_t id, uint8_t is_extended, uint8_t *data, uint8_t len)
{
    can_transmit_message_struct tx_msg;
    uint8_t i;

    if (len > 8U) {
        len = 8U;
    }

    if (0U != is_extended) {
        tx_msg.tx_ff = (uint8_t)CAN_FF_EXTENDED;
        tx_msg.tx_efid = id;           // 扩展帧 ID (29位)
        tx_msg.tx_sfid = 0U;
    } else {
        tx_msg.tx_ff = (uint8_t)CAN_FF_STANDARD;
        tx_msg.tx_sfid = id & 0x7FFU;  // 标准帧只有 11 位
        tx_msg.tx_efid = 0U;
    }

    tx_msg.tx_ft = CAN_FT_DATA;        // 数据帧
    tx_msg.tx_dlen = len;              // 字节长度 (0~8)

    for (i = 0U; i < len; i++) {
        tx_msg.tx_data[i] = data[i];
    }

    return can_message_transmit(CAN0, &tx_msg);
}

/* 发送标准帧 */
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t len)
{
    return can0_send_frame(id, 0U, data, len);
}

/* 从软件 FIFO 取一帧（主循环上下文） */
uint8_t can0_recv_msg(can_receive_message_struct *rx_msg)
{
    if ((NULL == rx_msg) || (can_rx_head == can_rx_tail)) {
        return 0U;      // FIFO 空
    }

    *rx_msg = can_rx_fifo[can_rx_tail];
    can_rx_tail = (uint8_t)((can_rx_tail + 1U) % CAN_RX_FIFO_DEPTH);

    return 1U;
}

/* 丢帧计数 */
uint32_t can0_rx_drop_count(void)
{
    return can_rx_drop;
}

/* 把一帧报文打印到调试串口 */
void can0_print_msg(const can_receive_message_struct *rx_msg)
{
    uint8_t i;

    printf("[CAN RX] ID:0x%03X, DLC:%d, Data: ", rx_msg->rx_sfid, rx_msg->rx_dlen);
    for (i = 0; i < rx_msg->rx_dlen; i++) {
        printf("%02X ", rx_msg->rx_data[i]);
    }
    printf("\r\n");
}
