#ifndef APP_TASK_H
#define APP_TASK_H

#include "gd32f30x.h"

/* 板级外设与 USB 协议栈初始化 */
void app_init(void);

/* 主循环调度：USB 轮询 + 周期心跳 + CAN 接收 */
void app_poll(void);

#endif /* APP_TASK_H */
