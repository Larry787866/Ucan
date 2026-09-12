/*!
    \file    main.c
    \brief   GD32F303CBT6 USB-CAN Custom Board Main Function
*/

#include "gd32f30x.h"
#include "systick.h"
#include "app_task.h"

int main(void)
{
    /* 系统滴答：delay_1ms() 和任务节拍的基础，必须最先初始化 */
    systick_config();

    /* 板级外设 + USB 协议栈初始化 */
    app_init();

    /* 主循环只做调度，具体任务都在 app_task.c 里 */
    while (1)
    {
        app_poll();
        delay_1ms(10);
    }
}
