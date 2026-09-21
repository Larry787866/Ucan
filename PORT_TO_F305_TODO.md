# 迁移到 GD32F305RCT6（CL / USBFS）—— Keil 侧待办

文件已经整理好了，剩下这些必须在 **Keil 里手工改**（代码文件见下面的"已完成"）。

## 一、Options for Target → Device

| 项 | 现在 | 改成 |
|---|---|---|
| Device | `GD32F303ZE` | `GD32F305RC` |
| Define | `USE_STDPERIPH_DRIVER GD32F30X_HD` | `USE_STDPERIPH_DRIVER GD32F30X_CL` |
| 启动文件 | `startup_gd32f30x_hd.s` | `startup_gd32f30x_cl.s`（已在 basic/Src/，路径 `..\basic\Src\startup_gd32f30x_cl.s`） |
| Flash 算法 | F303ZE | 随器件自动换成 F305RC（256KB） |

## 二、文件组（旧的 7 个路径已失效，必须删）

**删除**（文件已退役到 `legacy_hd/`）：
```
..\User\src\gd32f30x_usbd_hw.c
..\basic\Src\gd32f303e_eval.c
..\basic\Src\startup_gd32f30x_hd.s
..\usbd\Source\usbd_lld_int.c
..\usbd\Source\usbd_lld_core.c
..\device\Source\usbd_core.c
..\device\Source\usbd_enum.c
..\device\Source\usbd_pwr.c
..\device\Source\usbd_transc.c
..\class\device\cdc\Source\cdc_acm_core.c
```

**加入**（都在工程目录里，直接 Add Existing）：
```
..\User\src\gd32f30x_hw.c
..\driver\Source\drv_usb_core.c
..\driver\Source\drv_usb_dev.c
..\driver\Source\drv_usbd_int.c
..\device\core\Source\usbd_core.c
..\device\core\Source\usbd_enum.c
..\device\core\Source\usbd_transc.c
..\device\class\cdc\Source\cdc_acm_core.c
```
（`host/`、`driver/Source/drv_usb_host.c`、`drv_usbh_int.c` 是主机栈，设备模式不用加）

## 三、Options for Target → C/C++ → Include Paths

**加**：
```
..\driver\Include
..\device\core\Include
..\device\class\cdc\Include
..\ustd\common
..\ustd\class\cdc
```
**删**：`..\usbd\Include`、`..\device\Include`（已不存在）；`..\basic\Inc` 可以留着也可以删。

## 四、代码里还要改的（我没动你的源码）

1. `User/src/gd32f30x_it.c`
   - 删掉 `USBD_LP_CAN0_RX0_IRQHandler` / `USBD_HP_CAN0_TX_IRQHandler` 整段
   - 加：
     ```c
     extern usb_core_driver cdc_acm;   /* 或 extern usb_dev cdc_acm; 见 main.c 的类型 */

     void USBFS_IRQHandler(void)
     {
         usbd_isr(&cdc_acm);
     }
     ```
   - **必须再加一个**（否则 `usbd_connect()` 里的 `usb_mdelay(3)` 会死等，USB 连不上）：
     ```c
     extern void usb_timer_irq(void);

     void TIMER2_IRQHandler(void)
     {
         usb_timer_irq();
     }
     ```
     （如果不想占用 TIMER2，把 `User/src/gd32f30x_hw.c` 里的 `usb_mdelay()` 改成调用 `systick.h` 的 `delay_1ms()`、`usb_timer_init()` 留空即可，那就不要加 TIMER2_IRQHandler）

2. `User/src/main.c`
   - `usb_dev usbd_cdc;` → `usb_core_driver cdc_acm;`
   - 初始化顺序改成：
     ```c
     usb_rcu_config();
     usb_timer_init();
     usbd_init(&cdc_acm, &cdc_desc, &cdc_class);
     usb_intr_config();
     systick_config();
     usbd_connect(&cdc_acm);
     ```
   - 枚举等待：`while (USBD_CONFIGURED != cdc_acm.dev.cur_status)`（新栈是 `.dev.cur_status`）
   - 主循环里 `cdc_acm_check_ready(&cdc_acm)` / `cdc_acm_data_receive` / `cdc_acm_data_send` 名字不变，直接用
   - `#include "usbd_hw.h"` → `#include "drv_usb_hw.h"`

3. `Conf/Src/can.c:14`：`GPIO_CAN_PARTIAL_REMAP` → `GPIO_CAN0_PARTIAL_REMAP`
   （编码值相同，PB8=CAN0_RX / PB9=CAN0_TX 的部分重映射不变）

4. 可以直接把 `// can0_config();` 和 `// can0_send_test();` 的注释放开了：
   F305 上 USBFS 在 0x50000000 段有独立 FIFO RAM，和 CAN0 的 0x40006400 不再共用 SRAM。

5. 收尾清理（可选）：`gd32f30x_sdio.c`、`gd32f30x_exmc.c`、`gd32f30x_enet.c` 这些本项目不用的
   外设驱动可以从文件组里移掉。

## 五、硬件侧

- LQFP48 → **LQFP64**，板子要改
- 删掉 PA10 那路 USB 上拉三极管电路，PA11(DM)/PA12(DP) 直连
- 核对 `Conf/Inc/gpio.h` 里的 LED 引脚（PA4/PA5）在新板上还成立
