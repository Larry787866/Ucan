#include "app_task.h"
#include "bsp_gpio.h"
#include "bsp_usart1.h"
#include "bsp_can.h"
#include "bsp_usbd.h"
#include "Protocal.h"

#include <stdio.h>

/* 主循环节拍 (ms)，需与 main() 里的 delay_1ms() 保持一致 */
#define APP_TICK_MS         10U
/* 心跳周期 (ms) */
#define APP_HEARTBEAT_MS    1000U
/* 一次从 USB 取走的最大字节数（CDC 端点单包上限） */
#define APP_USB_RX_BUF_LEN  64U

/* CAN 测试心跳帧 */
static uint8_t test_frame[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

void app_init(void)
{
    bsp_gpio_init();            /* PA4(LED1)、PA5(LED2)、PA10(USB 开关) */
    bsp_usart1_init(115200);    /* PA2/PA3 调试串口 */
    bsp_print_clock_info();
    bsp_can0_init();            /* PB8/PB9 CAN0 (1 Mbps) */
    protocol_init();            /* USB-CAN 协议解析器 */
    bsp_usbd_init();            /* 含 DP 软复位与枚举时序 */
}

/* 每 1 秒：LED1 心跳 + 发一帧 CAN 测试数据
   这帧会被 CAN->USB 任务打包送回电脑，等于顺带自测了整条链路 */
static void app_heartbeat_task(void)
{
    led1_toggle();

    can0_send_msg(0x200, test_frame, sizeof(test_frame));
    printf("[CAN TX] Sent ID: 0x200\r\n");
}

/* 方向 1：电脑 -> CAN。取走 USB 下发的字节，喂给协议状态机 */
static void app_usb_to_can_task(void)
{
    uint8_t buf[APP_USB_RX_BUF_LEN];
    uint16_t n;

    n = bsp_usbd_recv(buf, sizeof(buf));
    if (0U != n) {
        protocol_usb_feed(buf, n);
    }

    protocol_usb_to_can_process();
}

/* 方向 2：CAN -> 电脑 */
static void app_can_to_usb_task(void)
{
    can_receive_message_struct rx_msg;

    if (0U == can0_recv_msg(&rx_msg)) {
        return;
    }

    led2_toggle();
    can0_print_msg(&rx_msg);            /* 调试串口留一份给人看的 */
    protocol_can_to_usb_send(&rx_msg);  /* 协议帧送回电脑 */
}

void app_poll(void)
{
    static uint32_t tick = 0U;

    bsp_usbd_poll();        /* 维护枚举状态并武装 OUT 端点，必须最先调用 */

    /* 1 秒周期任务 */
    tick += APP_TICK_MS;
    if (tick >= APP_HEARTBEAT_MS) {
        tick = 0U;
        app_heartbeat_task();
    }

    app_usb_to_can_task();
    app_can_to_usb_task();
}
