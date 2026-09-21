#include "Func.h"
#include "gpio.h"
#include "systick.h"

extern volatile uint32_t systick_tick;

void led_toggle(uint32_t port, uint32_t pin)
{
    gpio_bit_write(port, pin, (bit_status)(1 - gpio_input_bit_get(port, pin)));
}

void led_water(void)
{
    static uint32_t last = 0U;

    if ((systick_tick - last) < 500U)
    {
        return;
    }
    last = systick_tick;

    uint8_t led_index = 0U;
    for (led_index = 0U; led_index < 4U; led_index++)
    {
        if (0U != led_index)
        {
            gpio_bit_reset(Led_GPIO_PORT, (uint32_t)(1U << (led_index + 3U)));
        }
        else
        {
            gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN4);
        }
        gpio_bit_set(Led_GPIO_PORT, (uint32_t)(1U << (led_index + 3U)));
    }
}

void led_progress(void)
{
    static uint32_t last = 0U;

    if ((systick_tick - last) <500U)
    {
        return;
    }
    last = systick_tick;

    uint8_t led_index = 0U;
    for (led_index = 0U; led_index < 4U; led_index += 2U)
    {
        gpio_bit_reset(Led_GPIO_PORT, (uint32_t)(1U << (led_index)));
        gpio_bit_reset(Led_GPIO_PORT, (uint32_t)(1U << (led_index + 1U)));
        gpio_bit_set(Led_GPIO_PORT, (uint32_t)(1U << (led_index)));
        gpio_bit_set(Led_GPIO_PORT, (uint32_t)(1U << (led_index + 1U)));
    }
}

void led_complete(void)
{
    static uint32_t last = 0U;

    if ((systick_tick - last) < 200U)
    {
        return;
    }
    last = systick_tick;
    gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN1 | Led_GPIO_PIN2 | Led_GPIO_PIN3 | Led_GPIO_PIN4);
    gpio_bit_set(Led_GPIO_PORT, Led_GPIO_PIN1 | Led_GPIO_PIN2 | Led_GPIO_PIN3 | Led_GPIO_PIN4);
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
        led_progress();
        break;
    case LED_Complete:
        led_complete();
        break;
    default:
        break;
    }
}
