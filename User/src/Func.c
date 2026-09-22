#include "Func.h"
#include "gpio.h"
#include "systick.h"

/* 四个 LED 的位掩码（PB4~PB7） */
#define LED_ALL_PINS    (Led_GPIO_PIN1 | Led_GPIO_PIN2 | Led_GPIO_PIN3 | Led_GPIO_PIN4)

extern volatile uint32_t systick_tick;

void led_toggle(uint32_t port, uint32_t pin)
{
    gpio_bit_write(port, pin, (bit_status)(1 - gpio_input_bit_get(port, pin)));
}

/* 单个灯轮流点亮，0.5s 一步 */
void led_water(void)
{
    static uint32_t last = 0U;
    static uint8_t  pos  = 0U;

    if ((systick_tick - last) < 500U)
    {
        return;
    }
    last = systick_tick;

    gpio_bit_reset(Led_GPIO_PORT, LED_ALL_PINS);
    gpio_bit_set(Led_GPIO_PORT, (uint32_t)(1U << (pos + 4U)));

    pos++;
    if (pos >= 4U)
    {
        pos = 0U;
    }
}

/* 两两交替闪烁，0.25s 一步 */
void led_progress(void)
{
    static uint32_t last = 0U;
    static uint8_t  half = 0U;

    if ((systick_tick - last) < 250U)
    {
        return;
    }
    last = systick_tick;

    half ^= 1U;
    if (0U != half)
    {
        gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN3 | Led_GPIO_PIN4);
        gpio_bit_set(Led_GPIO_PORT, Led_GPIO_PIN1 | Led_GPIO_PIN2);
    }
    else
    {
        gpio_bit_reset(Led_GPIO_PORT, Led_GPIO_PIN1 | Led_GPIO_PIN2);
        gpio_bit_set(Led_GPIO_PORT, Led_GPIO_PIN3 | Led_GPIO_PIN4);
    }
}

/* 四个一起闪，0.2s 一步 */
void led_complete(void)
{
    static uint32_t last = 0U;
    static uint8_t  on   = 0U;

    if ((systick_tick - last) < 200U)
    {
        return;
    }
    last = systick_tick;

    on ^= 1U;
    if (0U != on)
    {
        gpio_bit_set(Led_GPIO_PORT, LED_ALL_PINS);
    }
    else
    {
        gpio_bit_reset(Led_GPIO_PORT, LED_ALL_PINS);
    }
}

void led_state(LED_State state)
{
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
