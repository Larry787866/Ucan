# Ucan —— USB ↔ CAN0 适配器固件

| 项目 | 说明 |
|---|---|
| 芯片 | GD32F305RCT6（`GD32F30X_CL`） |
| 工具链 | Keil MDK / ARMCC V5 |
| 库 | GD32F30x 标准外设库 V3.0.3 |
| 工程 | `MDK-ARM/cdc_acm.uvprojx` |

固件里有**两套 USB 类，二选一**，在 `User/src/main.c` 里切换（把不用的那个函数调用注释掉）：

| 模式 | 板子在电脑眼里是什么 | 上位机 | 说明 |
|---|---|---|---|
| **CDC**（默认） | 虚拟串口 | VOFA+ / 任意串口工具 | 自己定的变长帧协议（v2），见 [三、帧格式](#三帧格式) |
| **gs_usb** | 通用 USB-CAN 适配器（candleLight / WinUSB） | Linux `candump`、Windows Cangaroo | gs_usb 协议，免驱，见 [十、gs_usb 模式](#十gs_usb-模式) |

CDC 模式下，往串口写一帧就发一帧 CAN，CAN 总线上收到一帧就往串口吐一帧，两个方向的格式一样。

> 本工程基于 GigaDevice GD32F30x 固件库的 USB CDC_ACM 例程修改而来（Copyright (c) 2025, GigaDevice Semiconductor Inc.）。USB 部分改动：设备口从 USBD 换到 USBFS，在 CDC 串口之上加了 CAN0 转发和自定义协议层，另外新增了一套 gs_usb（candleLight）vendor 类。

---

## 目录

- [Ucan —— USB ↔ CAN0 适配器固件](#ucan--usb--can0-适配器固件)
  - [目录](#目录)
  - [一、硬件与时钟](#一硬件与时钟)
    - [1.1 引脚](#11-引脚)
    - [1.2 时钟](#12-时钟)
    - [1.3 CAN 比特率](#13-can-比特率)
  - [二、USB 侧](#二usb-侧)
  - [三、帧格式](#三帧格式)
    - [3.1 电脑 → CAN](#31-电脑--can)
    - [3.2 CAN → 电脑](#32-can--电脑)
    - [3.3 握手（命令帧）](#33-握手命令帧)
    - [3.4 一个例子](#34-一个例子)
    - [3.5 上位机解析要注意的几点](#35-上位机解析要注意的几点)
  - [四、CAN 侧配置](#四can-侧配置)
  - [五、LED](#五led)
  - [六、已知行为与限制](#六已知行为与限制)
  - [七、代码结构](#七代码结构)
  - [八、调试开关与排查](#八调试开关与排查)
    - [8.1 自动测试发送](#81-自动测试发送)
    - [8.2 数 CAN 收了几个帧](#82-数-can-收了几个帧)
    - [8.3 一个帧都收不到的时候，按这个顺序查](#83-一个帧都收不到的时候按这个顺序查)
  - [九、上位机（Cangaroo / VOFA+）](#九上位机cangaroo--vofa)
    - [9.1 Cangaroo](#91-cangaroo)
    - [9.2 VOFA+](#92-vofa)
  - [十、gs_usb 模式](#十gs_usb-模式)
    - [10.1 怎么切换](#101-怎么切换)
    - [10.2 USB 侧](#102-usb-侧)
    - [10.3 Linux 下怎么用](#103-linux-下怎么用)
    - [10.4 Windows 下怎么用](#104-windows-下怎么用)
    - [10.5 支持的功能](#105-支持的功能)
    - [10.6 已知限制](#106-已知限制)
    - [10.7 自测（不需要第二个节点）](#107-自测不需要第二个节点)
    - [10.8 排查](#108-排查)

---

## 一、硬件与时钟

### 1.1 引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| USB | PA11 = DM，PA12 = DP | USBFS，48 MHz |
| CAN0 | PB8 = CAN0_RX，PB9 = CAN0_TX | 走 `GPIO_CAN0_PARTIAL_REMAP`；PB8 上拉输入，PB9 复用推挽 |
| LED | PB4 / PB5 / PB6 / PB7 | 推挽输出，固件里置高点亮 |
| SWD | PA13 = SWDIO，PA14 = SWCLK | `GPIO_SWJ_SWDPENABLE_REMAP`，只留 SWD |

> ⚠️ CAN0 默认引脚就是 PA11/PA12，被 USB 占了，所以必须重映射到 PB8/PB9。这一段在 `Conf/Src/can.c` 的 `can0_config()` 里，改引脚时别忘了 remap 也要跟着改。

### 1.2 时钟

| 项目 | 值 |
|---|---|
| 晶振 | 25 MHz（HXTAL，CL 系列） |
| 系统时钟 | `__SYSTEM_CLOCK_120M_PLL_HXTAL` → CK_SYS = 120 MHz |
| APB1 | 60 MHz |
| CK_USBFS | 48 MHz |

### 1.3 CAN 比特率

CAN0 挂在 APB1（60 MHz）上：

```text
60 MHz / prescaler 5 / (1 + BS1 8TQ + BS2 3TQ = 12TQ) = 1 Mbps
SJW = 1TQ
```

> ⚠️ CDC 模式下是**固定 1 Mbps**。对面节点（Cangaroo / 另一个适配器）也必须是 1 Mbps，对不上的话两个方向都不通。改比特率就改 `Conf/Src/can.c` 里的 `prescaler`、`time_segment_1`、`time_segment_2` 三个值。
>
> gs_usb 模式下比特率是**上位机说了算的**（Cangaroo 里选、Linux 下 `ip link ... bitrate`），开机默认值仍是这里的 1 Mbps，只作为没配置过时的兜底。

---

## 二、USB 侧

> 本节说的是 **CDC 模式**（`main()` 里调 `app_run_cdc()` 时）。gs_usb 模式的 VID/PID 和端点都不一样，见 [十、gs_usb 模式](#十gs_usb-模式)。

枚举成 USB CDC ACM 虚拟串口：

| 项目 | 值 |
|---|---|
| VID / PID | `0x28E9` / `0x018A` |
| 厂商字符串 | `GigaDevice` |
| 产品字符串 | `GD32-CDC_ACM` |

端点：

| 端点 | 大小 | 用途 |
|---|---|---|
| EP1 IN | 64 字节 | 数据上行（板子 → 电脑） |
| EP3 OUT | 64 字节 | 数据下行（电脑 → 板子） |
| EP2 IN | 8 字节 | CDC 控制端点 |

> ⚠️ **串口参数全被忽略。** 波特率、数据位、校验位、流控都是摆设——数据不做任何串口层面的编码，就是裸字节流。上位机随便设（比如 115200 8N1）都能用。

---

## 三、帧格式

> **v2 格式，不向后兼容。** 旧版是固定 18 字节、带 `0xEE` 帧尾和 SUM 校验、DATA 补 0 到 8 字节——那套已经废弃了。按旧格式写的老脚本/模板都要改。

**变长帧**，多字节字段都是大端（高位在前）：

| 偏移 | 长度 | 内容 |
|---|---|---|
| `[0]` | 1 | `0xAA` 帧头 1 |
| `[1]` | 1 | `0x55` 帧头 2 |
| `[2]` | 1 | **高 4 位 = 类型，低 4 位 = 长度** |
| `[3]` | 2 或 4 | CAN ID（大端）：标准帧 2 字节，扩展帧 4 字节 |
| `[3+idlen]` | DLC | DATA，**DLC 个字节，比 8 小不补 0** |

类型：

| 类型 | 名字 | 总长 | 说明 |
|---|---|---|---|
| `1` | 标准数据帧 | **5 + DLC**（5 ~ 13） | ID 2 字节 |
| `2` | 扩展数据帧 | **7 + DLC**（7 ~ 15） | ID 4 字节 |
| `3` | 命令帧 | **4 + L**（4 ~ 19） | `AA 55 3L <cmd> <payload[L]>` |
| `4` / `5` | 保留 | — | 将来的标准/扩展远程帧，现在收到按非法帧处理 |

**没有帧尾，没有 SUM 校验**（USB 每个包本身就有 CRC16），长度完全由 `[2]` 推出来。

### 3.1 电脑 → CAN

`[2]` 的高 4 位决定发标准帧还是扩展帧，低 4 位就是 DLC，后面跟着 ID 和数据。板子收到后直接调 `can0_send_frame()` 发到 CAN 总线上。

- **DLC 可以是 0**（空帧）：标准帧 `AA 55 10 01 23` 就是 ID `0x123`、没有数据。
- DLC > 8 的低 4 位（`9`~`F`）算非法帧，按 [3.5](#35-上位机解析要注意的几点) 的规则丢掉。
- 往 CAN 方向**只能发数据帧**，远程帧还发不了（类型 4/5 保留）。

### 3.2 CAN → 电脑

CAN 收到的帧按同样的格式组包发给电脑：标准帧用 `rx_sfid`（2 字节 ID），扩展帧用 `rx_efid`（4 字节 ID），数据只发 DLC 个字节。

> ⚠️ **没握手就不上报。** CAN 帧要先确认链路已经握手（见 [3.3](#33-握手命令帧)）才会往电脑发，没握手时收到的 CAN 帧直接丢掉。

### 3.3 握手（命令帧）

命令帧的格式是 `AA 55 3L <cmd> <payload[L]>`：`[2]` 的高 4 位固定是 `3`，低 4 位是 payload 长度 `L`；`[3]` 是命令码，后面是 `L` 字节 payload。

握手用的命令码是 `0x01`：

| 方向 | 字节 | 说明 |
|---|---|---|
| 电脑 → 板子 | `AA 55 30 01` | 握手请求（L = 0） |
| 板子 → 电脑 | `AA 55 34 01 01 10 0F 01` | 握手应答（L = 4） |

应答的 4 个 payload 字节：

| 字节 | 值 | 含义 |
|---|---|---|
| `[4]` | `01` | 协议版本 0.1 |
| `[5]` | `10` | 固件版本 1.0 |
| `[6]` | `0F` | 能力位：bit0 扩展帧 / bit1 命令帧 / bit2 允许 DLC = 0 / bit3 需要握手 |
| `[7]` | `01` | 当前 CAN 比特率索引：`1` = 1 Mbps |

规则：

1. **上电 / USB 重新枚举之后是"未握手"状态，CAN → 电脑不上报。** 这样电脑上收到的每一个字节都肯定是我们故意发出去的，杂散字节不会被当成假帧。
2. 收到握手请求 → 板子回应答 → 进入已握手，开始上报。
3. 收到任何**合法数据帧**也算已握手（宽容：不想写握手的脚本，直接发数据帧也能用）。
4. 解析出错**只丢 1 个字节**继续往后扫，不像旧版那样整段丢 18 字节。
5. USB 挂起/恢复不影响握手状态；只有真的重新枚举（掉线重插）才回到未握手。

### 3.4 一个例子

```text
AA 55 12 04 56 AB CD

帧头    AA 55
[2]     12          高 4 位 1 = 标准数据帧，低 4 位 2 = DLC 2
ID      04 56   ->  0x456（2 字节大端）
DATA    AB CD       DLC 个字节，不补 0
```

一共 **7 个字节**，结果是：CAN 总线上出现一帧 **标准帧 ID = 0x456、DLC = 2、数据 `AB CD`**。

同一帧用旧格式要 18 字节（`AA 55 01 00 00 04 56 02 AB CD 00 00 00 00 00 00 D5 EE`），现在省掉 11 个字节。

### 3.5 上位机解析要注意的几点

- USB CDC 是**字节流**，不保证和帧边界对齐：
  - 板子往下发是一帧一个 USB 包，但上位机一次 `read` 可能读到好几帧连在一起，别假设"读一次就是一帧"。
  - 往板子方向，可以一次 `write` 一帧，也可以把好几帧拼在一起写（一个 USB 包 64 字节，短帧能塞十几帧）；一帧被拆到两个包里写也行，板子的状态机是跨包的。
  - 结论：上位机自己维护接收缓冲，按 `AA 55` + `[2]` 算长度来重组。
- 一帧最长 15 字节（扩展帧 DLC 8），所以一个 64 字节的 USB 包最多装 4 帧。
- DATA 里如果正好出现 `AA 55`，会被当成帧头——但新格式**会算长度、错了只丢 1 字节重扫**，所以拼错的帧最多影响一小段，不会像旧版那样连着丢 18 字节。
- **不做握手的上位机也能收**（发一帧数据就算握手了），但上电后必须**先发点什么**才会开始收到上行帧。

---

## 四、CAN 侧配置

`Conf/Src/can.c` 里的 `can0_config()`：

| 配置项 | 值 |
|---|---|
| 工作模式 | `CAN_NORMAL_MODE` |
| 自动重发 | `ENABLE`（没人应答时硬件会一直重发） |
| bus-off 自动恢复 | `ENABLE` |
| FIFO 覆盖 | `DISABLE` |
| 验收过滤器 | 32 位掩码模式，list / mask 全 0 → **全通**，收到的报文都进 FIFO0 |

也就是说：总线上任何 ID、标准帧/扩展帧都会被收下来（远程帧例外，见 [六](#六已知行为与限制)）。

gs_usb 模式下上位机还能改这几项（`can0_reconfigure()`，改了不用重新编译）：

| 上位机要改的东西 | 落到哪里 |
|---|---|
| 比特率（`bitrate` / Cangaroo 里的下拉框） | `CAN_BT` 的 prescaler / BS1 / BS2 / SJW |
| Listen Only | `CAN_SILENT_MODE` |
| Loopback | `CAN_LOOPBACK_MODE`（Listen Only + Loopback 就是 `CAN_SILENT_LOOPBACK_MODE`） |
| One-Shot | `auto_retrans = DISABLE` |
| Stop / Start | `CAN_MODE_INITIALIZE` / 回到上面那几种工作模式 |

`can0_reconfigure()` 只重写 `CAN_CTL` / `CAN_BT`，**不碰验收滤波器**，所以 `can0_config()` 里配的全通滤波器改完照样有效。

---

## 五、LED

PB4 ~ PB7 四个灯，三种状态（`User/src/Func.c`）：

| 状态 | 表现 | 判定条件 |
|---|---|---|
| 空闲 | 一个灯轮流亮，走马灯，500 ms 一步 | 没有数据流动 |
| 正在传输 | 两两交替闪，250 ms 一步 | 最后一次收发数据之后 300 ms 内 |
| 刚传完 | 四个一起亮灭，200 ms 一步 | 最后一帧之后 1.3 秒内 |

PC → CAN 和 CAN → PC 两个方向的数据都会推到"正在传"，所以灯只能告诉你"有数据在动"，不能区分是哪个方向。

---

## 六、已知行为与限制

1. **远程帧（RTR）不上报。** CAN 总线上收到的远程帧会被丢掉，电脑收不到。CDC 协议里的类型 4/5 就是留给它的，但还没实现；往 CAN 方向也不支持远程帧——从电脑发过来的永远按数据帧发。
2. **上行队列 16 帧**（CAN → 电脑）。USB 侧堵住（比如上位机不读）时队列一满就直接丢帧，不阻塞。
3. **CAN0 FIFO0 只有 3 级深**，靠主循环轮询取走。主循环如果被长时间占住，就可能丢 CAN 帧。
4. **发送不阻塞。** `can0_send_frame()` 只是把报文投进邮箱就返回；三个邮箱都占着不空时（典型情况是对面掉线、硬件在无限重发），这一帧会被丢掉，返回 `CAN_TRANSMIT_NOMAILBOX`。因此 `CAN_TRANSMIT_OK` 只代表"已经投递给硬件"，**不代表已经上了总线，更不代表被 ACK 了**。
5. **CAN 的错误状态**（错误计数、bus-off）不上报给电脑（gs_usb 模式下也没有错误帧上报）。要看得进调试器读寄存器。
6. **一次从 USB 最多读 64 字节**（`USB_CDC_RX_LEN`），主循环每轮处理这么多。
7. **CDC 模式下没握手就不上报**（见 [3.3](#33-握手命令帧)）。上电后 VOFA+ 里看不到任何 CAN 帧是正常的，先发一帧（握手请求或随便一帧数据）就行。
8. **两个模式共用一套硬件，只能二选一。** CDC 的上行端点和 gs_usb 的 `0x81` 是同一个物理端点，同时注册两个类会打架；`main()` 里只调一个。

---

## 七、代码结构

| 路径 | 内容 |
|---|---|
| `User/src/main.c` | 初始化顺序 + **两个模式二选一** + 各自的主循环 |
| `User/src/Protocal.c/.h` | **CDC 协议层（v2）**：变长帧解析与组包、握手、USB 环形缓冲、上行队列、LED 状态判断 |
| `User/src/gsusb.c/.h` | **gs_usb（candleLight）类**：描述符、WCID、控制请求、帧队列、CAN 转发 |
| `Conf/Src/can.c/.h` | CAN0 初始化、收发封装、`can0_reconfigure()` / `can0_send_frame_ex()` |
| `device/core/Source/usbd_enum_ucan.c` | **GD32 库 `usbd_enum.c` 的副本**（加了两处改动，见下） |
| `device/` 其余 | USB 设备核心 + CDC 类（`cdc_acm_core.c` 等） |
| `Conf/Src/gpio.c/.h` | 板载 LED 的 GPIO |
| `User/src/Func.c/.h` | 灯的三种闪法 |
| `User/src/systick.c` | 1 ms 系统滴答，`get_systick_tick()` 返回毫秒 |
| `driver/` | USBFS 底层驱动 |
| `GD32F30x_standard_peripheral/` | 厂商外设库（一个字节都没动） |

### 7.1 `usbd_enum_ucan.c` 是库文件的副本

`device/core/Source/usbd_enum.c` 是全库唯一必须改的文件，但**没有直接改它**，而是复制成 `usbd_enum_ucan.c`，在副本上加了这两处：

1. `usbd_vendor_request()`：原版是空壳（无条件返回 `REQ_SUPP`，核心随后拿着没人填的缓冲区去收发 = 野指针）。改成转发给当前类。
2. `_usb_std_getdescriptor()` 的字符串分支：给 Windows 的 `0xEE`（MS OS 描述符）开个口子，交给当前类去填——WCID 免驱要用。

Keil 工程里 `USBFS` 组编译的是这份副本（`usbd_enum.c` 已从工程里移除）。**这两处对 CDC 完全无害**：CDC 的上位机不发 vendor 请求，就算发了 `cdc_acm_req` 也是无条件返回 `USBD_OK`，行为和改动前一样。将来升级 GD32 库时，拿这份副本和新的原文件 diff 一下就能重新合。

> ⚠️ 函数名必须保持原样（`usbd_transc.c` 是按名字调它的），所以**两份不能同时编译**。

### 7.2 初始化顺序（`main.c`）

```text
USB 时钟 -> USB 定时器 -> LED -> systick -> can0_config
-> app_run_xxx()：
      usbd_init(实例, 描述符, 类) -> USB 中断 -> usbd_connect
      -> protocol_init() / gsusb_init()
      -> 等枚举完成（USBD_CONFIGURED）-> 主循环
```

- `usb_intr_config()` 必须在 `usbd_init()` **之后**：`usbd_init` 里才给 `class_core` 赋值，中断抢在它前面进来会解引用空指针。
- 两个模式共用同一个 `usb_core_driver` 实例（`main.c` 的 `cdc_acm`），所以 `gd32f30x_it.c` 里的 `usbd_isr(&cdc_acm)` 一行都不用改。
- 枚举没完成之前不做任何转发。

### 7.3 主循环

| 模式 | 循环体 |
|---|---|
| CDC | `protocol_task()` + 按 `protocol_activity()` 点灯 |
| gs_usb | `gsusb_task()`（内部点灯） |

`protocol_task()` 每轮做三件事：

1. 电脑 → CAN：读 USB、解帧、`can0_send_frame()`
2. CAN → 电脑：`can_rx_poll()` 取空 FIFO0（未握手就丢）、进上行队列
3. 握手应答 + `can_up_flush()` 把上行队列发走

**一条铁律**：CAN 寄存器的读写只在主循环里做，USB 中断里只搬数据、只记状态。gs_usb 那套也一样（MODE/BITTIMING 收到先记下，真正的 `can_init` 留给 `gsusb_task()`）。

---

## 八、调试开关与排查

### 8.1 自动测试发送

`User/inc/Protocal.h` 里的 `PROTO_AUTO_TEST`：

| 值 | 行为 |
|---|---|
| `0` | 关闭（正式固件就是这个值） |
| `1` | 每秒往 VOFA 灌一帧假数据，完全不碰 CAN（单独验证 USB 上行通路） |
| `2` | 每秒往 CAN 总线上发一帧 ID = `0x123`（数据前两字节是递增序号，用来数 Cangaroo 里收到几次） |

周期由 `PROTO_AUTO_TEST_MS` 控制，默认 1000 ms。

### 8.2 数 CAN 收了几个帧

`User/src/Protocal.c` 里的 `can_rx_test()`：把 FIFO0 取空，并把结果记在 `can_rx_count` / `can_rx_last_id` 两个全局变量里，在 Keil 的 Watch 窗口加上这两个就能看。

默认 `main.c` 里**没有调用**它。要用就在主循环最前面加一行 `can_rx_test();`（它会先于 `protocol_task()` 把 FIFO0 取空，所以加上的时候转发是由它来做的）。

> `can_rx_count` 数的是"CAN 控制器收到了几帧"，和握不握手无关，所以拿它判断物理层通不通最干净。
> 但帧要真的发到 VOFA+ 上，还得先握手（`proto_link_ready = 1`）。Keil 的 Watch 窗口里把 `can_rx_count` 和 `proto_link_ready` 一起加上最省事。

### 8.3 一个帧都收不到的时候，按这个顺序查

**a. 先查对面节点，别急着拆硬件。**
Cangaroo 之类的工具如果在 **Listen Only / 只听模式**，它既不发帧也不给出 ACK——表现就是板子这边报 ACK 错误、一个字节都收不到。这是踩过的坑。确认对面是 Normal 模式、比特率 1 Mbps。

**b. 加 `can_rx_test()` 看 `can_rx_count`。**
对面发一帧它就该 +1；一直不动说明物理层没有字节进来（收发器、PB8、接线、终端电阻、共地）。

**c. 直接读 CAN0 的寄存器**（Keil Watch 里加 `*(unsigned int *)0x40006404` 这样看）：

| 地址 | 寄存器 | 位 | 含义 |
|---|---|---|---|
| `0x40006418` | CAN_ERR | `bit4-6` ERRN | 错误码。= 3 是 **ACK 错误**：帧干净地发出去了但没人应答 → 查对面节点；= 1/2/6 是填充/格式/CRC 错误 → 查收发器和接线 |
| | | `bit16-23` TECNT | 发送错误计数 |
| | | `bit24-31` RECNT | 接收错误计数。一直是 0 说明板子从没在总线上见到过别人的报文 |
| | | `bit1` PERR | 错误被动（历史上错误累计到过 128 以上） |
| `0x40006404` | CAN_STAT | `bit11` RXL | RX 引脚当前电平。= 1 是隐性（总线空闲），说明总线没被卡死在显性 |
| | | `bit8` TS | 控制器正在发送 |
| `0x4000640C` | CAN_RFIFO0 | | = 0 **不能**说明没收到报文——固件每个主循环都把它读空，采到 0 是正常的 |

**d. 注意 TECNT 归零不代表没出过错。**
没人应答时硬件会一路重发到 bus-off，而 `auto_bus_off_recovery` 会自动恢复并清掉 TECNT。所以"**TECNT = 0 且 PERR = 1**"恰恰说明中间严重错过。

---

## 九、上位机（Cangaroo / VOFA+）

板子在上位机眼里就是一个虚拟串口，所以上位机本身不受限。实际联调用的是这两个：

| 工具 | 干什么 | 怎么接 |
|---|---|---|
| **Cangaroo** | CAN 收发：看总线上有什么帧、手动发帧、周期发帧 | 比特率 **1 Mbps**，模式 **Normal** |
| **VOFA+** | 串口终端：直接看 / 直接发原始帧（HEX） | 打开对应的 COM 口，串口参数随意；先发 `AA 55 30 01` 握手 |

### 9.1 Cangaroo

- 比特率必须 **1 Mbps**，和板子的配置对上（算法见 [1.3](#13-can-比特率)）。
- 模式必须是 **Normal**。

> ⚠️ **Listen Only / 只听模式是踩过的坑。** 这个模式下它既不发帧、也不给出 ACK。板子那边的表现是：`CAN_ERR` 报 ACK 错误、一个字节都收不到、还因为没人应答一路重发到 bus-off（TECNT 又被自动恢复清零，看起来"没有错误"）。当时查了很久，最后发现是这边的设置问题。**收不到帧先来这儿看。**

- 想验证"板子能不能发出去"，把 `PROTO_AUTO_TEST` 设成 `2`（见 [8.1](#81-自动测试发送)），板子每秒往总线发一帧 ID = `0x123`，在 Cangaroo 的接收列表里数它出现几次。

### 9.2 VOFA+

- 波特率、数据位这些随便设，固件全忽略（见 [二](#二usb-侧)）。
- 接收区要按 **字节 / HEX** 看，不然是乱码。
- **先握手，再收发。** 打开串口后先发这一帧（4 字节）：

  ```text
  AA 55 30 01
  ```

  正常会立刻回一帧 8 字节的 `AA 55 34 01 01 10 0F 01`。没回说明固件没跑起来或者串口没选对。**不握手的话，板子不会往电脑发任何 CAN 帧**（见 [3.3](#33-握手命令帧)）。
- 然后就可以手发数据帧了，比如：

  ```text
  AA 55 12 04 56 AB CD
  ```

  这一帧的含义是"标准帧 ID = 0x456、DLC = 2、数据 `AB CD`"。
- 扩展帧：`[2]` 换成 `2x`（x = DLC），ID 写满 4 字节，比如

  ```text
  AA 55 24 12 34 56 78 AA BB CC DD
  ```

  这是"扩展帧 ID = 0x12345678、DLC = 4、数据 `AA BB CC DD`"，总长 11 字节（7 + 4）。
- 反过来，板子把 CAN 收到的帧也按同样的格式吐在这个串口上，所以 VOFA+ 里能直接看到总线上来了什么。发一帧、收一帧，两个方向的现象应该一一对应。
- 发 DLC = 0 的空帧：`AA 55 10 01 23`（标准帧 ID `0x123`、无数据）。

> ⚠️ 别忘了关掉 VOFA+ 的**周期发送 / 定时发送**。之前回环测试的时候它就开着，一帧被连着灌了好几遍，看起来像"发一帧回来了十几次"，白白怀疑了半天板子。

---

## 十、gs_usb 模式

板子另一种用法：不做虚拟串口，直接变成业界通用的 **USB-CAN 适配器**，走的是 candleLight 那套 **gs_usb** 协议。Linux 内核自带驱动，Windows 靠 WCID 自动绑 WinUSB，**两边都不用装驱动**。

代码在 `User/src/gsusb.c`（类实现）和 `User/inc/gsusb.h`。整个 CAN 转发逻辑和 CDC 模式共用 `Conf/Src/can.c` 里的封装。

### 10.1 怎么切换

`User/src/main.c` 的 `main()` 最后：

```c
    app_run_gsusb();
    /* app_run_cdc(); */
```

- 想用 gs_usb：保持这样。
- 想回 CDC：把 `app_run_gsusb();` 注释掉，打开 `app_run_cdc();`。

改完重新编译烧写。两个函数各自做 `usbd_init()` + 等枚举 + 自己的主循环，所以不需要任何编译期宏。

> ⚠️ **两个模式不能同时用。** 它们共用同一个 `usb_core_driver` 实例，而且 CDC 的上行端点和 gs_usb 的 `0x81` 是**同一个物理端点**（见 [10.6](#106-已知限制)）。

### 10.2 USB 侧

| 项目 | 值 |
|---|---|
| VID / PID | `0x1D50` / `0x606F` |
| 设备类 | vendor，接口 `0xFF` / `0xFF` / `0xFF`，单接口 |
| 端点 | EP1 IN = `0x81`（64 字节）、EP2 OUT = `0x02`（64 字节），都是 bulk |
| 字符串 | 厂商 `Ucan`，产品 `Ucan gs_usb` |
| 免驱 | WCID（微软 OS 描述符）：兼容 ID `WINUSB`，GUID `{c15b4308-04d3-11e6-b3ea-6057189e6443}` |

`0x81` / `0x02` 是 candleLight 定死的，Linux 内核驱动和 Cangaroo 都按这两个地址找端点，不能改。

数据帧是协议规定的**固定 20 字节**（小端）：`echo_id` 4 + `can_id` 4 + `can_dlc` 1 + `channel` 1 + `flags` 1 + `reserved` 1 + `data` 8。和 CDC 那套变长帧完全无关。

### 10.3 Linux 下怎么用

内核自带 `gs_usb` 驱动（多数发行版已经编好了，没有就 `sudo modprobe gs_usb`）：

```bash
lsusb -d 1d50:606f                  # 先确认设备在
sudo ip link set can0 up type can bitrate 1000000
candump can0                        # 收
cansend can0 123#AABBCCDD           # 发（标准帧）
cansend can0 12345678#1122334455667788   # 扩展帧
cangen can0 -g 10                   # 连续发
```

常用比特率直接填：`bitrate 125000` / `250000` / `500000` / `1000000`。改比特率要先把接口 `down` 再 `up`。范围由设备上报的 bittiming 能力决定（`brp` 上限 1024，所以最低约 2 kbps）。

其它能加的选项（`ip link set can0 up type can ...`）：

```bash
bitrate 500000 listen-only on       # 只听：不发也不给 ACK
bitrate 500000 loopback on          # 回环：自己发的自己收（不需要第二个节点）
bitrate 500000 one-shot on          # 一发不重试
```

### 10.4 Windows 下怎么用

**Cangaroo** 里选设备，一般情况下会出现 `Ucan gs_usb`。

- 首次插入时 Windows 会读 WCID 描述符，自动绑 WinUSB。设备管理器里应该能看到一个 `WinUsb Device`（没有黄色感叹号）。
- 如果显示成未知设备：右键 → 属性 → 详细信息 → **兼容 ID**，看有没有 `USB\MS_COMP_WINUSB`。有就是 WCID 生效了、只是驱动没绑上；没有就是 WCID 描述符的问题，回头查 `gsusb.c` 里那三块描述符。
- 比特率要在 **接口 up（Start）之前**选好，和我们上报的能力范围对得上。

### 10.5 支持的功能

| 功能 | 说明 |
|---|---|
| 比特率 | 上位机下发 bittiming，落到 `can0_reconfigure()` |
| Start / Stop | 对应 `CAN_MODE_NORMAL` / `CAN_MODE_INITIALIZE` |
| Listen Only / Loopback / One-Shot | 三种模式位，可组合（Listen Only + Loopback = `CAN_SILENT_LOOPBACK_MODE`） |
| 收（CAN → 电脑） | 标准帧 / 扩展帧 / 远程帧（远程帧带 `CAN_RTR_FLAG`） |
| 发（电脑 → CAN） | 标准帧 / 扩展帧 / 远程帧 |
| TX echo | 主机发来的帧原样回显（`echo_id` 保留），Cangaroo 靠它判断"已发送" |
| 时间戳查询 | 能读（SOF 累加的微秒计数），但帧里不带时间戳 |

### 10.6 已知限制

1. **没有硬件时间戳。** 帧固定 20 字节，没有帧尾那 4 字节时间戳，所以不 advertise `HW_TIMESTAMP`。
2. **没有错误帧上报。** CAN 错误（bus-off、错误计数）不会变成 `CAN_ERR_*` 帧；带 `CAN_ERR_FLAG` 的帧也不会当成错误上报。相关的 feature 位都没开（`berr-reporting` 这些选项在内核层面就会被拒掉）。
3. **没有终端电阻控制、没有硬件验收滤波器下发**（`SET_FILTER`）、没有 `GET_STATE`、没有 CAN FD。CAN 侧仍然是**全通滤波器**，总线上所有帧都收。
4. **发送不阻塞。** 主机发来的帧投邮箱失败（三个邮箱都占着，典型情况是对面掉线在无限重发）时**不会被丢掉也不会硬报成功**——它留在队列里下一轮重试，上位机那边看到的是"还没发送完成"。总线上没人应答时这个现象是真实的，不是 bug。
5. **两个模式共用 `0x81`。** CDC 的上行端点就是 `0x81`，所以两个类只能二选一；哪天要做复合设备，得先动 `usb_class_core` 的单指针结构。
6. **`usb_class_core` 只有一块 `class_data[4]`**，USB 中断里也不碰 CAN 寄存器（MODE / BITTIMING 收到先记下，真正的 `can_init` 在主循环里做）。

### 10.7 自测（不需要第二个节点）

用 **Loopback** 模式，硬件自己回环，不需要对面给 ACK：

- Linux：

  ```bash
  sudo ip link set can0 down
  sudo ip link set can0 up type can bitrate 1000000 loopback on
  candump can0 &
  cansend can0 123#AABBCCDD
  ```

  应该立刻在 `candump` 里看到自己刚发的 `123#AABBCCDD`。看不到就是 USB 侧或者 CAN 侧没通，和对面节点无关。

- Windows：Cangaroo 里把模式设成 **Loopback**，手动发一帧，看它有没有被标成"已发送"并且出现在接收列表里。

### 10.8 排查

| 现象 | 先查什么 |
|---|---|
| Cangaroo 一直"pending"、发不出去 | TX echo 没走通。Keil Watch 里看 `gs->tx_busy`、`to_host` 的 head/tail；`can0_send_frame_ex()` 的返回值是不是 `CAN_TRANSMIT_OK` |
| 上位机能开设备但收不到 CAN 帧 | 总线上有没有帧（`can_rx_count`）、比特率和对面是否一致、模式是不是 Listen Only |
| `ip link set can0 up` 报错 | dmesg 看内核驱动报了什么；比特率是不是超出上报的能力范围（`brp` 1 ~ 1024） |
| Windows 认成未知设备 | 见 [10.4](#104-windows-下怎么用)：先看兼容 ID 里有没有 `USB\MS_COMP_WINUSB` |
