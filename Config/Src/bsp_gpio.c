#include "bsp_gpio.h"

void bsp_gpio_init(void)
{
    // 1. 开启 GPIOA 时钟
    rcu_periph_clock_enable(RCU_GPIOA);

    // 2. 配置 PA4 (LED1) 和 PA5 (LED2) 为推挽输出，默认输出高电平（灭灯）
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_4 | GPIO_PIN_5);
    gpio_bit_set(GPIOA, GPIO_PIN_4);
    gpio_bit_set(GPIOA, GPIO_PIN_5);

    // 3. 配置 PA10 为推挽输出（控制 USB DP 1.5k 上拉开关）
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_10);
    gpio_bit_reset(GPIOA, GPIO_PIN_10); // 上电默认拉低，断开 USB
}

void led1_toggle(void)
{
    gpio_bit_write(GPIOA, GPIO_PIN_4, (bit_status)(1 - gpio_output_bit_get(GPIOA, GPIO_PIN_4)));
}

void led2_toggle(void)
{
    gpio_bit_write(GPIOA, GPIO_PIN_5, (bit_status)(1 - gpio_output_bit_get(GPIOA, GPIO_PIN_5)));
}

void usb_soft_connect(uint8_t enable)
{
    if (enable) {
        gpio_bit_set(GPIOA, GPIO_PIN_10);   // PA10 置 1：接通 1.5k 上拉，通知电脑开始枚举
    } else {
        gpio_bit_reset(GPIOA, GPIO_PIN_10); // PA10 置 0：断开上拉，模拟拔出 USB 线
    }
}
