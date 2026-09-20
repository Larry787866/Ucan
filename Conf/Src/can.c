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

    gpio_pin_remap_config(GPIO_CAN_PARTIAL_REMAP, ENABLE);

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

uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t send_len)
{
    can_transmit_message_struct tx_message;
    uint32_t timeout = 0U;
    uint8_t i = 0U;
    uint8_t mailbox = CAN_NOMAILBOX;

    if ((0 == data) || (0U == send_len)) {
        return CAN_TRANSMIT_FAILED;
    }
    if (send_len > 8U) {
        send_len = 8U;
    }

    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &tx_message);

    tx_message.tx_sfid = id & 0x7FFU;
    tx_message.tx_efid = 0x00U;
    tx_message.tx_ff   = CAN_FF_STANDARD;
    tx_message.tx_ft   = CAN_FT_DATA;
    tx_message.tx_dlen = send_len;

    for (i = 0U; i < send_len; i++) {
        tx_message.tx_data[i] = data[i];
    }

    mailbox = can_message_transmit(CAN0, &tx_message);
    if (CAN_NOMAILBOX == mailbox) {
        return CAN_TRANSMIT_NOMAILBOX;
    }

    timeout = 0xFFFFFU;
    while ((CAN_TRANSMIT_PENDING == can_transmit_states(CAN0, mailbox)) && (0U != timeout)) {
        timeout--;
    }

    if (CAN_TRANSMIT_PENDING == can_transmit_states(CAN0, mailbox)) {
        can_transmission_stop(CAN0, mailbox);
        return CAN_TRANSMIT_TIMEOUT; /* transmit timeout */
    }

    return (uint8_t)can_transmit_states(CAN0, mailbox);
}


uint8_t can0_send_test(void)
{
    uint8_t cmd_data[]    = {0x01, 0x02, 0x03, 0x04};
    uint8_t sensor_data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t state = CAN_TRANSMIT_FAILED;

    state = can0_send_msg(0x123, cmd_data, (uint8_t)sizeof(cmd_data));
    if (CAN_TRANSMIT_OK != state) {
        return state;                      
    }

    state = can0_send_msg(0x200, sensor_data, (uint8_t)sizeof(sensor_data));
    if (CAN_TRANSMIT_OK != state) {
        return state;
    }
    state = can0_send_msg(0x300, sensor_data, 4U);

    return state;
}
