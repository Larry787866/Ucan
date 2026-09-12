#include "bsp_can.h"

void bsp_can0_init(void)
{
    can_parameter_struct can_parameter;
    can_filter_parameter_struct can_filter;

    /* 1. 开启时钟：GPIOB、AFIO复用时钟、CAN0外设时钟 */
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_CAN0);

    /* 2. 引脚配置：将 CAN0 重映射到 PB8 和 PB9 */
    gpio_pin_remap_config(GPIO_CAN_PARTIAL_REMAP, ENABLE);

    // PB9 (TX): 复用推挽输出 (AF_PP)
    gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    // PB8 (RX): 上拉输入 (IPU)
    gpio_init(GPIOB, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_8);

    /* 3. 基础控制器参数配置（对应你参考代码中的 Init 部分） */
    can_deinit(CAN0);
    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);

    can_parameter.time_triggered = DISABLE;          // 关闭时间触发模式
    can_parameter.auto_bus_off_recovery = ENABLE;    // 自动离线恢复 (对应 AutoBusOff = ENABLE)
    can_parameter.auto_wake_up = ENABLE;             // 自动唤醒 (对应 AutoWakeUp = ENABLE)
    can_parameter.auto_retrans = ENABLE;             // 自动重传 (对应 AutoRetransmission = ENABLE)
    can_parameter.rec_fifo_overwrite = DISABLE;      // 接收 FIFO 溢出不覆盖
    can_parameter.trans_fifo_order = DISABLE;        // 发送优先级由报文 ID 决定
    can_parameter.working_mode = CAN_NORMAL_MODE;    // 正常总线通信模式


    can_parameter.prescaler = 6;
    can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
    can_parameter.time_segment_1 = CAN_BT_BS1_7TQ;
    can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
    can_init(CAN0, &can_parameter);

    /* 4. 最基础的“全通”过滤器配置
       注意：CAN 硬件规定必须激活至少一个过滤器，掩码全部填 0 即代表“无过滤，全部放行” */
    can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);
    can_filter.filter_number = 0;                    // 使用第 0 组过滤器
    can_filter.filter_mode = CAN_FILTERMODE_MASK;    // 掩码屏蔽模式
    can_filter.filter_bits = CAN_FILTERBITS_32BIT;   // 32 位全宽
    can_filter.filter_list_high = 0x0000;            // 匹配 ID 设为 0
    can_filter.filter_list_low  = 0x0000;
    can_filter.filter_mask_high = 0x0000;            // 掩码设为 0（不关心任何位，全部放行）
    can_filter.filter_mask_low  = 0x0000;
    can_filter.filter_fifo_number = CAN_FIFO0;       // 存入 FIFO0 邮箱
    can_filter.filter_enable = ENABLE;               // 激活过滤器
    can_filter_init(&can_filter);
}

/* 发送一帧数据 */
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t len)
{
    can_transmit_message_struct tx_msg;
    uint8_t i;

    tx_msg.tx_sfid = id;               // 标准帧 ID
    tx_msg.tx_efid = 0x00;
    tx_msg.tx_ff = CAN_FF_STANDARD;    // 标准帧格式
    tx_msg.tx_ft = CAN_FT_DATA;        // 数据帧
    tx_msg.tx_dlen = len;              // 字节长度 (0~8)

    for (i = 0; i < len; i++) {
        tx_msg.tx_data[i] = data[i];
    }

    return can_message_transmit(CAN0, &tx_msg);
}
/* 接收一帧数据（轮询读取） */
uint8_t can0_recv_msg(can_receive_message_struct *rx_msg)
{
    // 检查 FIFO0 是否有报文挂起等待读取
    if (can_receive_message_length_get(CAN0, CAN_FIFO0) > 0) {
        can_message_receive(CAN0, CAN_FIFO0, rx_msg);
        return 1; // 读到报文
    }
    return 0; // 无报文
}
