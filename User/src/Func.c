#include "Func.h"
#include "gpio.h"
#include "systick.h"

void led_water(void)
{
    led_gpio_config();
    gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN1);
    gpio_bit_set(Led_GPIO_PORT, Led_GPIO_PIN2);
    delay_1ms(1000);
    led_toggle(Led_GPIO_PORT, Led_GPIO_PIN1);
    led_toggle(Led_GPIO_PORT, Led_GPIO_PIN2);
    delay_1ms(1000);
}

void led_toggle(uint32_t port, uint32_t pin)
{
    gpio_bit_write(port, pin, (bit_status)(1 - gpio_input_bit_get(port, pin)));
}

void led_state(LED_State state)
{
    led_gpio_config();
    switch (state)
    {
    case lED_IDLE:
        led_water();
        break;
    case LED_Progress:
        gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN1);
        gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN2);
        delay_1ms(500);
        led_toggle(Led_GPIO_PORT, Led_GPIO_PIN1);
        delay_1ms(500);
        break;
    case LED_Complete:
        gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN1);
        gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN2);
        delay_1ms(500);
        led_toggle(Led_GPIO_PORT, Led_GPIO_PIN1);
        led_toggle(Led_GPIO_PORT, Led_GPIO_PIN2);
        delay_1ms(500);
        break;
    default:
        break;
    }
}
