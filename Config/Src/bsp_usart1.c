#include "bsp_usart1.h"
#include "bsp_gpio.h"
#include "systick.h"

void bsp_usart1_init(uint32_t baudrate)
{
    // 1. 开启 GPIOA 和 USART1 外设时钟
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART1);

    // 2. PA2 (TX): 复用推挽输出 (AF_PP)
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

    // 3. PA3 (RX): 浮空输入 (IN_FLOATING)
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_3);

    // 4. 串口参数配置：8N1
    usart_deinit(USART1);
    usart_baudrate_set(USART1, baudrate);
    usart_word_length_set(USART1, USART_WL_8BIT);
    usart_stop_bit_set(USART1, USART_STB_1BIT);
    usart_parity_config(USART1, USART_PM_NONE);
    usart_hardware_flow_rts_config(USART1, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART1, USART_CTS_DISABLE);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_enable(USART1);
}

void Usb_Start()
{
    printf("[USB] Resetting USB DP pull-up...\r\n");
    usb_soft_connect(0); 
    delay_1ms(200);
    usb_soft_connect(1);   
}
// 重定向 C 语言 printf 到 USART1
int fputc(int ch, FILE *f)
{
    usart_data_transmit(USART1, (uint8_t)ch);
    while (RESET == usart_flag_get(USART1, USART_FLAG_TBE));
    return ch;
}
