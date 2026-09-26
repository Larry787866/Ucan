#include "can.h"
#include "gd32f30x.h"

void can0_config(void)
{
    can_parameter_struct can_parameter;
    can_filter_parameter_struct can_filter;

    /* enable CAN clock */
    rcu_periph_clock_enable(RCU_CAN0);
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_GPIOB);

    gpio_pin_remap_config(GPIO_CAN0_PARTIAL_REMAP, ENABLE);

    /* configure CAN0 GPIO */
    gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    gpio_init(GPIOB, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_8);

    /* initialize CAN register */
    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);
    can_parameter.working_mode = CAN_NORMAL_MODE;
    can_parameter.auto_bus_off_recovery = ENABLE;
    can_parameter.auto_wake_up = DISABLE;
    can_parameter.auto_retrans = ENABLE;
    can_parameter.rec_fifo_overwrite = DISABLE;
    can_parameter.trans_fifo_order = DISABLE;
    can_parameter.time_triggered = DISABLE;

    can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
    can_parameter.time_segment_1 = CAN_BT_BS1_8TQ;
    can_parameter.time_segment_2 = CAN_BT_BS2_3TQ;
    can_parameter.prescaler = 5;

    can_init(CAN0, &can_parameter);

    /* initialize filter */
    can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);
    can_filter.filter_number = 0;

    can_filter.filter_mode = CAN_FILTERMODE_MASK;
    can_filter.filter_bits = CAN_FILTERBITS_32BIT;

    can_filter.filter_list_high = 0x0000U;
    can_filter.filter_list_low = 0x0000U;
    can_filter.filter_mask_high = 0x0000U;
    can_filter.filter_mask_low = 0x0000U;

    can_filter.filter_fifo_number = CAN_FIFO0;
    can_filter.filter_enable = ENABLE;
    can_filter_init(&can_filter);
}

void can0_stop(void)
{
    /* 进初始化模式：收发都停，但不复位外设，滤波器保持原样 */
    (void)can_working_mode_set(CAN0, CAN_MODE_INITIALIZE);
}

ErrStatus can0_reconfigure(uint32_t brp, uint32_t tseg1, uint32_t tseg2, uint32_t sjw,
                           uint8_t listen_only, uint8_t loopback, uint8_t one_shot)
{
    can_parameter_struct can_parameter;

    /* 位时序字段越界：给上位机回 ERROR，让它自己换算去，别写进寄存器 */
    if ((brp < 1U) || (brp > 1024U) || (tseg1 > 15U) || (tseg2 > 7U) || (sjw > 3U))
    {
        return ERROR;
    }

    /* 这三种模式只能通过 can_init() 的 working_mode 字段写进 CAN_BT 的
       MODE 位（can_working_mode_set 只管 INITIALIZE / NORMAL / SLEEP） */
    if (0U != listen_only)
    {
        can_parameter.working_mode = (0U != loopback) ? CAN_SILENT_LOOPBACK_MODE : CAN_SILENT_MODE;
    }
    else
    {
        can_parameter.working_mode = (0U != loopback) ? CAN_LOOPBACK_MODE : CAN_NORMAL_MODE;
    }

    /* 其余字段保持 can0_config() 里的选择，只有位时序和自动重发按参数走 */
    can_parameter.auto_bus_off_recovery = ENABLE;
    can_parameter.auto_wake_up = DISABLE;
    /* one_shot：硬件不自动重发，发失败就发一次算完。
       库内部 auto_retrans == ENABLE 是清 CAN_CTL_ARD，所以这里反过来写。 */
    can_parameter.auto_retrans = (0U != one_shot) ? DISABLE : ENABLE;
    can_parameter.rec_fifo_overwrite = DISABLE;
    can_parameter.trans_fifo_order = DISABLE;
    can_parameter.time_triggered = DISABLE;

    can_parameter.resync_jump_width = (uint8_t)sjw;
    can_parameter.time_segment_1 = (uint8_t)tseg1;
    can_parameter.time_segment_2 = (uint8_t)tseg2;
    can_parameter.prescaler = (uint16_t)brp;

    /* can_init() 不碰滤波寄存器，can0_config() 里配的全通滤波器继续有效 */
    return can_init(CAN0, &can_parameter);
}

uint8_t can0_send_frame_ex(uint32_t id, uint8_t is_extended, uint8_t is_remote,
                           uint8_t *data, uint8_t send_len)
{
    can_transmit_message_struct tx_message;
    uint8_t i = 0U;
    uint8_t mailbox = CAN_NOMAILBOX;

    if (send_len > 8U)
    {
        send_len = 8U;
    }
    /* send_len 允许为 0（DLC = 0 的空帧）；只有要带数据时才要求 data 非空 */
    if ((0U != send_len) && (0 == data))
    {
        return CAN_TRANSMIT_FAILED;
    }

    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &tx_message);

    if (0U != is_extended)
    {
        tx_message.tx_efid = id & 0x1FFFFFFFU;
        tx_message.tx_sfid = 0x00U;
        tx_message.tx_ff = CAN_FF_EXTENDED;
    }
    else
    {
        tx_message.tx_sfid = id & 0x7FFU;
        tx_message.tx_efid = 0x00U;
        tx_message.tx_ff = CAN_FF_STANDARD;
    }
    tx_message.tx_ft = (0U != is_remote) ? CAN_FT_REMOTE : CAN_FT_DATA;
    tx_message.tx_dlen = send_len;

    for (i = 0U; i < send_len; i++)
    {
        tx_message.tx_data[i] = data[i];
    }

    mailbox = can_message_transmit(CAN0, &tx_message);
    if (CAN_NOMAILBOX == mailbox)
    {
        /* 三个邮箱都占着不空（多半是对面不应答、auto_retrans 在无限重发），
           这帧只能丢——那种情况下本来也发不出去 */
        return CAN_TRANSMIT_NOMAILBOX;
    }

    /* 投递完就返回，不再死等。
       can_message_transmit() 是同步把整帧写进 TMI/TMP/TMDATA 寄存器的，
       函数返回后报文就归硬件管了，跟 tx_message 和调用者的缓冲区都没关系；
       硬件自己发、自己按 auto_retrans 重发。
       （以前死等 0xFFFFF 次，对面不应答时每帧都把主循环卡住 0.2~0.3 秒，
       期间 CAN 的 3 级 FIFO 会溢出、USB OUT 也可能丢数据。）
       注意：CAN_TRANSMIT_OK 现在只表示"投递成功"，
             不再表示"已上总线并且被 ACK"。 */
    return CAN_TRANSMIT_OK;
}

uint8_t can0_send_frame(uint32_t id, uint8_t is_extended, uint8_t *data, uint8_t send_len)
{
    return can0_send_frame_ex(id, is_extended, 0U, data, send_len);
}

uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t send_len)
{
    return can0_send_frame(id, 0U, data, send_len);
}

uint8_t can0_recv_msg(can_receive_message_struct *rx_msg)
{
    if (0 == rx_msg)
    {
        return 0U;
    }

    if (0U == can_receive_message_length_get(CAN0, CAN_FIFO0))
    {
        return 0U; /* FIFO0 里没有报文 */
    }

    /* can_message_receive() 读走报文后会自己释放 FIFO */
    can_message_receive(CAN0, CAN_FIFO0, rx_msg);

    return 1U;
}

uint8_t can0_send_test(void)
{
    uint8_t cmd_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t sensor_data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t state = CAN_TRANSMIT_FAILED;

    state = can0_send_msg(0x123, cmd_data, (uint8_t)sizeof(cmd_data));
    if (CAN_TRANSMIT_OK != state)
    {
        return state;
    }

    state = can0_send_msg(0x200, sensor_data, (uint8_t)sizeof(sensor_data));
    if (CAN_TRANSMIT_OK != state)
    {
        return state;
    }
    state = can0_send_msg(0x300, sensor_data, 4U);

    return state;
}
