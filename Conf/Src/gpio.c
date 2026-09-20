#include "gpio.h"
#include "gd32f30x.h"

void led_gpio_config(void)
{
    /* enable the led clock */
    rcu_periph_clock_enable(Led_GPIO_CLK);
    /* configure led GPIO port */ 
    gpio_init(Led_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, Led_GPIO_PIN1 | Led_GPIO_PIN2);
}
