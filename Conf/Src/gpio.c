#include "gpio.h"
#include "gd32f30x.h"

void led_gpio_config(void)
{
    /* enable the led clock */
    rcu_periph_clock_enable(Led_GPIO_CLK);

    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);

    /* configure led GPIO port */
    gpio_init(Led_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              Led_GPIO_PIN1 | Led_GPIO_PIN2 | Led_GPIO_PIN3 | Led_GPIO_PIN4);
    gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN1 | Led_GPIO_PIN2 | Led_GPIO_PIN3 | Led_GPIO_PIN4);
}
