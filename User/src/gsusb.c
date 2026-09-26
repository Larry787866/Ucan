/*!
    \file    gsusb.c
    \brief   USB vendor 类：gs_usb (candleLight) 协议（Ucan）

    \version 2026-9-26

    这个类的意义：把板子做成"业界通用的 USB-CAN 适配器"，上位机不用自己写协议。
      Linux  ：内核自带 drivers/net/can/usb/gs_usb.c，插上就是一张 can0 网卡
               （ip link set can0 up type can bitrate 1000000；candump can0）
      Windows：靠 WCID（微软 OS 描述符）自动绑 WinUSB，Cangaroo 里直接能选到
    VID/PID 用 gs_usb 约定的 0x1D50 / 0x606F，内核和 Cangaroo 就认这两个数。

    协议来源：candle-usb/candleLight_fw 的 include/gs_usb.h、src/usbd_gs_can.c、
              src/can_common.c，以及 Linux 内核 drivers/net/can/usb/gs_usb.c。
              下面所有常量和字节数组都是照抄的，没有自己发明的东西。

    ⚠ 改这个文件之前先看这两条：
      1. USB 中断里（req_proc / ctlx_out / data_out / data_in / SOF）**不碰 CAN 寄存器**，
         只动内存里的队列和标志；真正配 CAN、发 CAN、发 USB 全都放在 gsusb_task() 里。
         中断里碰 CAN 的结果是"主机每发一个控制请求，CAN 就抽一下"，很难查。
      2. 帧一律 memcpy 进队列，绝不只存指针——OUT 的缓冲下一包就要复用。

    ⚠ 和 CDC 模式二选一：两个类的 IN 端点都是 0x81。main.c 里
      app_run_gsusb() / app_run_cdc() 只能开一个（详见 main.c 的注释）。
*/

#include "gsusb.h"
#include "usbd_enum.h"
#include "can.h"
#include "Func.h"

#include <string.h>

/* 和 Protocal.c 一样：只装了这一个类，复用的就是 main.c 里那个核心实例，
   gd32f30x_it.c 的 usbd_isr(&cdc_acm) 一行都不用改（它是类无关的）*/
extern usb_core_driver cdc_acm;

/* gs_usb 定死的 VID/PID */
#define GSUSB_VID                   0x1D50U
#define GSUSB_PID                   0x606FU

/* Windows 的 MS OS 描述符用这个厂商码（0x20）；和 gs_usb 自己的 bRequest 0..7 不撞 */
#define GSUSB_VENDOR_CODE           0x20U

/* CAN 时钟：GD32 的 CAN0 挂 APB1 = 60 MHz。主机拿这个数算比特率。 */
#define GSUSB_FCLK_CAN              60000000U

/* 总线上收来的帧，echo_id 固定填这个值（表示"这不是主机发的帧的回响"） */
#define GSUSB_ECHO_ID_RX            0xFFFFFFFFU

/* 主机没发 MODE 时的占位值 */
#define GSUSB_NO_PENDING            0xFFU

/* 类数据挂在 class_data 的哪一格（CDC 用的是 CDC_COM_INTERFACE = 0，同一格，
   因为同一时刻只装一个类） */
#define GSUSB_CLASS_IDX             0U

/* ---- 以下常量全部照抄 gs_usb.h，名字保持一致，方便和上游对照 ------------- */

/* bRequest（vendor 请求，bmRequestType = 0x41 主机→设备 / 0xC1 设备→主机） */
#define GS_USB_BREQ_HOST_FORMAT     0U
#define GS_USB_BREQ_BITTIMING       1U
#define GS_USB_BREQ_MODE            2U
#define GS_USB_BREQ_BT_CONST        4U
#define GS_USB_BREQ_DEVICE_CONFIG   5U
#define GS_USB_BREQ_TIMESTAMP       6U
#define GS_USB_BREQ_IDENTIFY        7U

/* 通道模式 */
#define GS_CAN_MODE_RESET           0U
#define GS_CAN_MODE_START           1U

/* 功能位：BT_CONST 里报给主机的。只报真支持的三个，不报的位主机就不会用。 */
#define GS_CAN_FEATURE_LISTEN_ONLY  (1U << 0)
#define GS_CAN_FEATURE_LOOP_BACK    (1U << 1)
#define GS_CAN_FEATURE_ONE_SHOT     (1U << 3)

#define GSUSB_FEATURE               (GS_CAN_FEATURE_LISTEN_ONLY | \
                                     GS_CAN_FEATURE_LOOP_BACK   | \
                                     GS_CAN_FEATURE_ONE_SHOT)

/* can_id 的高位标志 */
#define CAN_EFF_FLAG                0x80000000U   /* 扩展帧 */
#define CAN_RTR_FLAG                0x40000000U   /* 远程帧 */

/* 队列深度：主机 → CAN 的少一点（主机发得快，邮箱只有 3 个）；
   CAN → 主机的多一点（总线上一旦爆发，USB 上行跟不上） */
#define GSUSB_FH_DEPTH              8U
#define GSUSB_TH_DEPTH              16U

/* ------------------------------------------------------------------------ */
/* USB 描述符                                                                */
/* ------------------------------------------------------------------------ */

/* USB 标准设备描述符。
   ⚠ bcdUSB 必须是 0x0200：写成 0x0210 主机会来要 BOS 描述符，
     GD32 这套栈的 bos_desc 是 NULL，_usb_bos_desc_get() 会解引用空指针 → HardFault。 */
static const usb_desc_dev gsusb_dev_desc = {
    .header =
    {
        .bLength          = USB_DEV_DESC_LEN,
        .bDescriptorType  = USB_DESCTYPE_DEV
    },
    .bcdUSB                = 0x0200U,
    .bDeviceClass          = 0x00U,
    .bDeviceSubClass       = 0x00U,
    .bDeviceProtocol       = 0x00U,
    .bMaxPacketSize0       = USB_FS_EP0_MAX_LEN,
    .idVendor              = GSUSB_VID,
    .idProduct             = GSUSB_PID,
    /* ⚠ bcdDevice 是 WCID 缓存的键的一部分：Windows 把"这台 VID/PID/REV 有没有
       MS OS 描述符"记在
         HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<VID><PID><bcdDevice>\osvc
       里，而且**一辈子只问一次**（0xEE 那次）；答不对就写 osvc = 0000，
       之后插多少次都不再问。0x0100 那一格就是这么被写坏的，所以起步就抬到
       0x0101 换一格干净的；以后要是又改了 WCID 相关的东西，再 +1。 */
    .bcdDevice             = 0x0101U,
    .iManufacturer         = STR_IDX_MFC,
    .iProduct              = STR_IDX_PRODUCT,
    .iSerialNumber         = STR_IDX_SERIAL,
    .bNumberConfigurations = USBD_CFG_MAX_NUM
};

/* 配置描述符集合：配置 + 接口 + 两个端点，一共 9+9+7+7 = 32 字节。
   注意这几个描述符结构体在 usb_ch9_std.h 里是用 #pragma pack(1) 定义的
   （每个成员对齐都是 1），所以这个组合结构体不会被填充字节撑开。 */
typedef struct _gsusb_config_set {
    usb_desc_config config;
    usb_desc_itf    itf;
    usb_desc_ep     ep_in;
    usb_desc_ep     ep_out;
} gsusb_config_set;

/* bInterfaceClass 用 0xFF（厂商自定义）：gs_usb 就是靠这个 + VID/PID 认设备。
   接口号必须是 0 —— 内核驱动的 id_table 写的是
   USB_DEVICE_INTERFACE_NUMBER(0x1d50, 0x606f, 0)，接口号不是 0 根本不 probe。 */
static const gsusb_config_set gsusb_config_desc = {
    .config =
    {
        .header =
        {
            .bLength          = sizeof(usb_desc_config),
            .bDescriptorType  = USB_DESCTYPE_CONFIG
        },
        .wTotalLength         = sizeof(gsusb_config_set),
        .bNumInterfaces       = 0x01U,
        .bConfigurationValue  = 0x01U,
        .iConfiguration       = 0x00U,
        .bmAttributes         = 0x80U,   /* 总线供电（usbd_init 从 config_desc[7] 的 bit6 取） */
        .bMaxPower            = 0x32U    /* 100 mA */
    },

    .itf =
    {
        .header =
        {
            .bLength          = sizeof(usb_desc_itf),
            .bDescriptorType  = USB_DESCTYPE_ITF
        },
        .bInterfaceNumber     = 0x00U,
        .bAlternateSetting    = 0x00U,
        .bNumEndpoints        = 0x02U,
        .bInterfaceClass      = 0xFFU,
        .bInterfaceSubClass   = 0xFFU,
        .bInterfaceProtocol   = 0xFFU,
        .iInterface           = 0x00U
    },

    /* 0x81 == EP1_IN。candleLight 就固定用 0x81 / 0x02；
       0x81 正好是本芯片的 EP1_IN（TX1 FIFO 已经分了 64 words），
       0x02 是 EP2_OUT（OUT 端点不占专用 FIFO，共用 RX FIFO），
       所以从 CDC 切过来不需要改 usb_conf.h 里的 FIFO 预算。 */
    .ep_in =
    {
        .header =
        {
            .bLength          = sizeof(usb_desc_ep),
            .bDescriptorType  = USB_DESCTYPE_EP
        },
        .bEndpointAddress     = GSUSB_IN_EP,
        .bmAttributes         = USB_EP_ATTR_BULK,
        .wMaxPacketSize       = GSUSB_EP_SIZE,
        .bInterval            = 0x00U
    },

    .ep_out =
    {
        .header =
        {
            .bLength          = sizeof(usb_desc_ep),
            .bDescriptorType  = USB_DESCTYPE_EP
        },
        .bEndpointAddress     = GSUSB_OUT_EP,
        .bmAttributes         = USB_EP_ATTR_BULK,
        .wMaxPacketSize       = GSUSB_EP_SIZE,
        .bInterval            = 0x00U
    }
};

/* USB language ID Descriptor */
static const usb_desc_LANGID gsusb_language_id_desc = {
    .header =
    {
        .bLength         = sizeof(usb_desc_LANGID),
        .bDescriptorType = USB_DESCTYPE_STR
    },
    .wLANGID             = ENG_LANGID
};

/* 厂商字符串 */
static const usb_desc_str gsusb_manufacturer_string = {
    .header =
    {
        .bLength         = USB_STRING_LEN(4U),
        .bDescriptorType = USB_DESCTYPE_STR
    },
    .unicode_string = {'U', 'c', 'a', 'n'}
};

/* 产品字符串 */
static const usb_desc_str gsusb_product_string = {
    .header =
    {
        .bLength         = USB_STRING_LEN(11U),
        .bDescriptorType = USB_DESCTYPE_STR
    },
    .unicode_string = {'U', 'c', 'a', 'n', ' ', 'g', 's', '_', 'u', 's', 'b'}
};

/* 序列号字符串。⚠ 必须是**可写**的（不是 const）：usbd_init() 会把芯片的
   96 位唯一 ID 换算成 12 个十六进制字符写进 unicode_string[1..12]
   （usbd_core.c 的 serial_string_get），bLength 也要留够 12 个字符。 */
static usb_desc_str gsusb_serial_string = {
    .header =
    {
        .bLength         = USB_STRING_LEN(12U),
        .bDescriptorType = USB_DESCTYPE_STR
    }
};

static void *const gsusb_strings[] = {
    [STR_IDX_LANGID]  = (uint8_t *)&gsusb_language_id_desc,
    [STR_IDX_MFC]     = (uint8_t *)&gsusb_manufacturer_string,
    [STR_IDX_PRODUCT] = (uint8_t *)&gsusb_product_string,
    [STR_IDX_SERIAL]  = (uint8_t *)&gsusb_serial_string
};

usb_desc gsusb_desc = {
    .dev_desc    = (uint8_t *)&gsusb_dev_desc,
    .config_desc = (uint8_t *)&gsusb_config_desc,
    .strings     = gsusb_strings
};

/* ------------------------------------------------------------------------ */
/* WCID（微软 OS 描述符）：Windows 免驱的关键                                 */
/* 三块数据全部照抄 candleLight，只有一处必须改：Compatible ID 的接口数       */
/* ------------------------------------------------------------------------ */

/* 1. MS OS 字符串描述符，字符串索引 0xEE。
      主机拿到它才知道"要读厂商描述符时用 bRequest = 0x20"。
      18 字节 = 2 字节头 + "MSFT100"（7 个字符 14 字节）+ 1 字节厂商码 + 1 字节 0 */
static const uint8_t gsusb_ms_os_string[] = {
    0x12, 0x03,
    0x4D, 0x00, 0x53, 0x00, 0x46, 0x00, 0x54, 0x00, 0x31, 0x00, 0x30, 0x00, 0x30, 0x00,
    GSUSB_VENDOR_CODE,
    0x00
};

/* 2. Compatible ID 描述符（wIndex = 0x0004）。
      ⚠ 长度必须按"本设备只有 1 个接口"算成 40 字节：
        16 字节头 + 1 个 24 字节的节 = 40（0x28），bCount = 1。
        candleLight 原版是 64 字节 / bCount = 2（它有 CAN 接口 + DFU 接口），
        照抄过来的话 Windows 会认为还有第二个接口，描述符就对不上了。
      "WINUSB\0\0" 这个兼容 ID 就是"给我绑 WinUSB 驱动"的意思。 */
static const uint8_t gsusb_ms_comp_id[] = {
    0x28, 0x00, 0x00, 0x00,   /* dwLength = 40 */
    0x00, 0x01,               /* bcdVersion = 1.0 */
    0x04, 0x00,               /* wIndex = 0x0004 */
    0x01,                     /* bCount = 1（只有一个接口） */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* 7 字节保留 */
    0x00,                     /* 接口号 0 */
    0x01,                     /* 保留 */
    0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,   /* 兼容 ID "WINUSB\0\0" */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* 子兼容 ID（不用） */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00                 /* 6 字节保留 */
};

/* 3. 扩展属性描述符（wIndex = 0x0005）：给设备挂一个设备接口 GUID。
      Cangaroo / libusb 就是按这个 GUID 找设备的，所以这个 GUID 一个字都不能改
      （{c15b4308-04d3-11e6-b3ea-6057189e6443} 是 candleLight 约定的）。
      146 字节 = 10 字节头 + 1 个 136 字节的属性节。 */
static const uint8_t gsusb_ms_ext_prop[] = {
    0x92, 0x00, 0x00, 0x00,   /* dwLength = 146 */
    0x00, 0x01,               /* bcdVersion = 1.0 */
    0x05, 0x00,               /* wIndex = 0x0005 */
    0x01, 0x00,               /* bCount = 1 */
    0x88, 0x00, 0x00, 0x00,   /* 属性节长度 = 136 */
    0x07, 0x00, 0x00, 0x00,   /* 数据类型 7 = Unicode REG_MULTI_SZ */
    0x2A, 0x00,               /* 属性名长度 = 42 */

    0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00,   /* "DeviceInterfaceGUIDs" */
    0x63, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6E, 0x00,
    0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00,
    0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x47, 0x00,
    0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x73, 0x00,
    0x00, 0x00,

    0x50, 0x00, 0x00, 0x00,   /* 属性值长度 = 80 */

    0x7B, 0x00, 0x63, 0x00, 0x31, 0x00, 0x35, 0x00,   /* "{c15b4308-04d3-11e6- */
    0x62, 0x00, 0x34, 0x00, 0x33, 0x00, 0x30, 0x00,
    0x38, 0x00, 0x2D, 0x00, 0x30, 0x00, 0x34, 0x00,
    0x64, 0x00, 0x33, 0x00, 0x2D, 0x00, 0x31, 0x00,
    0x31, 0x00, 0x65, 0x00, 0x36, 0x00, 0x2D, 0x00,
    0x62, 0x00, 0x33, 0x00, 0x65, 0x00, 0x61, 0x00,
    0x2D, 0x00, 0x36, 0x00, 0x30, 0x00, 0x35, 0x00,   /* b3ea-6057 */
    0x37, 0x00, 0x31, 0x00, 0x38, 0x00, 0x39, 0x00,
    0x65, 0x00, 0x36, 0x00, 0x34, 0x00, 0x34, 0x00,
    0x33, 0x00, 0x7D, 0x00,                           /* 189e6443}" */
    0x00, 0x00, 0x00, 0x00                            /* 结尾两个 NUL */
};

/* ------------------------------------------------------------------------ */
/* 4. 能力与版本信息（BT_CONST / DEVICE_CONFIG 读的就是这两块）                */
/* ------------------------------------------------------------------------ */

/* 10 个 u32 = 40 字节。字段名和 gs_usb.h 里的 struct gs_device_bt_const 一一对应：
      feature, fclk_can, tseg1_min/max, tseg2_min/max, sjw_max, brp_min/max/inc
   全 u32 的结构体不会有填充字节，sizeof == 40。 */
typedef struct _gsusb_bt_const {
    uint32_t feature;
    uint32_t fclk_can;
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
} gsusb_bt_const_t;

/* 取值范围不是随便写的，要和 GD32 的 CAN_BT 寄存器字段对得上：
     tseg1 = prop_seg + phase_seg1 -> 寄存器字段 4 位，寄存器值 = tseg1 - 1 ∈ 0..15
                                    所以 tseg1 ∈ 1..16
     tseg2 -> 寄存器字段 3 位，寄存器值 = tseg2 - 1 ∈ 0..7，所以 tseg2 ∈ 1..8
     sjw   -> 寄存器字段 2 位，寄存器值 = sjw - 1 ∈ 0..3，所以 sjw_max = 4
     brp   -> 寄存器字段 10 位，库内部写 brp - 1 ∈ 0..1023，所以 brp ∈ 1..1024
   主机（Linux 的 can_calc_bittiming 或 Cangaroo）就是拿这四个上限去挑分频的，
   报大了它会算出一个写不进寄存器的组合。 */
static const gsusb_bt_const_t gsusb_bt_const = {
    .feature   = GSUSB_FEATURE,     /* 只报 LISTEN_ONLY / LOOP_BACK / ONE_SHOT */
    .fclk_can  = GSUSB_FCLK_CAN,    /* CAN0 挂 APB1，60 MHz */
    .tseg1_min = 1U,
    .tseg1_max = 16U,
    .tseg2_min = 1U,
    .tseg2_max = 8U,
    .sjw_max   = 4U,
    .brp_min   = 1U,
    .brp_max   = 1024U,
    .brp_inc   = 1U
};

/* 12 字节：3 个保留字节 + icount + sw_version + hw_version，全部小端。
   icount = 通道数 - 1 = 0（本板只有一路 CAN，通道号固定 0）。
   sw/hw 版本主机只是读出来显示，不校验，这里和 candleLight 取一样的值。 */
static const uint8_t gsusb_device_config[] = {
    0x00, 0x00, 0x00,             /* reserved1 ~ reserved3 */
    0x00,                         /* icount = 0 */
    0x02, 0x00, 0x00, 0x00,       /* sw_version = 2 */
    0x01, 0x00, 0x00, 0x00        /* hw_version = 1 */
};

/* ------------------------------------------------------------------------ */
/* 类句柄（约 650 字节 RAM）                                                  */
/* ------------------------------------------------------------------------ */

typedef struct _gs_usb_handler {
    /* ---- EP0 暂存 ---- */
    uint8_t  ep0_out_buf[64];                        /* 主机发下来的控制数据 */
    uint8_t  ts_buf[4];                              /* 读 TIMESTAMP 时的出参缓冲 */
    uint8_t  req_cmd;                                /* SETUP 阶段记下 bRequest，ctlx_out 里认数据用 */

    /* ---- 主机下发的 CAN 配置（主循环里才写进寄存器）---- */
    uint32_t bittiming[5];                           /* prop_seg / phase_seg1 / phase_seg2 / sjw / brp */
    uint32_t feature;                                /* MODE START 时带的功能位 */
    volatile uint8_t pending_mode;                   /* GSUSB_NO_PENDING / GS_CAN_MODE_RESET / _START */
    uint8_t  can_on;                                 /* CAN 控制器现在是否在工作（给 Watch 窗口看的） */

    /* ---- 时间戳：SOF 里每 1 ms 加 1000 µs ---- */
    volatile uint32_t timestamp_us;

    /* ---- 主机 → CAN 的队列：data_out（中断）填，gsusb_task 取 ---- */
    uint8_t  from_host[GSUSB_FH_DEPTH][GSUSB_FRAME_LEN];
    volatile uint8_t fh_head;                        /* 写入位置 */
    volatile uint8_t fh_tail;                        /* 读出位置 */
    volatile uint8_t fh_drop;                        /* 队列满了丢掉的帧数（调试用） */

    /* ---- CAN → 主机的队列：只有 gsusb_task 碰，head/tail 不用 volatile ---- */
    uint8_t  to_host[GSUSB_TH_DEPTH][GSUSB_FRAME_LEN];
    uint8_t  th_head;
    uint8_t  th_tail;
    uint8_t  th_drop;

    /* ---- 上行发送 ---- */
    uint8_t  tx_buf[GSUSB_FRAME_LEN];                /* ⚠ 必须是常驻缓冲：usbd_ep_send 只存指针 */
    volatile uint8_t tx_busy;                        /* 1 = 有一个 IN 传输在飞 */

    /* ---- OUT 端点收包缓冲 ---- */
    uint8_t  ep_out_buf[GSUSB_EP_SIZE];             /* ⚠ 同理：中断里往里收，不能是临时变量 */
} gs_usb_handler;

/*!
    \brief      取类句柄
    \retval     句柄指针；还没枚举完（class_data 没挂）时返回 NULL
*/
static gs_usb_handler *gs_get(void)
{
    return (gs_usb_handler *)cdc_acm.dev.class_data[GSUSB_CLASS_IDX];
}

/* little-endian 读写：gs_usb 的帧和结构体全部是小端，这里显式拼，
   不依赖编译器的字节序，也不做指针强转（避免未对齐访问） */
static uint32_t gs_u32_get(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void gs_u32_put(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/* 重新武装 OUT 端点。
   ⚠ 不重新武装就只能收一帧包；而且武装长度写 20 也会被 drv_usb_dev.c 抬成
     packet_count * max_len = 64，干脆直接写 64。 */
static void gs_out_rearm(usb_dev *udev)
{
    gs_usb_handler *gs = gs_get();

    if (NULL == gs) {
        return;
    }

    (void)usbd_ep_recev(udev, GSUSB_OUT_EP, gs->ep_out_buf, GSUSB_EP_SIZE);
}

/* ------------------------------------------------------------------------ */
/* 类回调                                                                    */
/* ------------------------------------------------------------------------ */

static uint8_t gsusb_class_init(usb_dev *udev, uint8_t config_index);
static uint8_t gsusb_class_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t gsusb_class_req(usb_dev *udev, usb_req *req);
static uint8_t gsusb_class_ctlx_out(usb_dev *udev);
static uint8_t gsusb_class_in(usb_dev *udev, uint8_t ep_num);
static uint8_t gsusb_class_out(usb_dev *udev, uint8_t ep_num);
static uint8_t gsusb_class_sof(usb_dev *udev);

/* USB gs_usb device class callbacks structure */
usb_class_core gsusb_class = {
    .command   = 0xFFU,          /* NO_CMD：本类没有类命令，和 CDC 一样用不到 */
    .alter_set = 0U,

    .init      = gsusb_class_init,
    .deinit    = gsusb_class_deinit,

    .req_proc  = gsusb_class_req,
    .ctlx_out  = gsusb_class_ctlx_out,
    .data_in   = gsusb_class_in,
    .data_out  = gsusb_class_out,
    .SOF       = gsusb_class_sof
};

/*!
    \brief      初始化 gs_usb 类（SET_CONFIGURATION 时被调用）
    \note       ⚠ USB reset 不会调 deinit，重新枚举时运行态只能在这里清干净。
*/
static uint8_t gsusb_class_init(usb_dev *udev, uint8_t config_index)
{
    static gs_usb_handler gs_handler;

    (void)config_index;

    memset(&gs_handler, 0, sizeof(gs_handler));

    /* 默认 1 Mbps，和 can0_config() 配的一致，换算回 gs_usb 的四个时间量是：
         60 MHz / brp 5 / (1 + (prop 0 + phase1 8) + phase2 3 TQ = 12 TQ) = 1 Mbps
       主机如果先发 BITTIMING 就会被覆盖；没发 BITTIMING 直接 START 时用这一组。 */
    gs_handler.bittiming[0] = 0U;   /* prop_seg */
    gs_handler.bittiming[1] = 8U;   /* phase_seg1（prop_seg + phase_seg1 = 8TQ） */
    gs_handler.bittiming[2] = 3U;   /* phase_seg2 */
    gs_handler.bittiming[3] = 1U;   /* sjw */
    gs_handler.bittiming[4] = 5U;   /* brp */

    gs_handler.req_cmd = 0xFFU;
    gs_handler.pending_mode = GSUSB_NO_PENDING;
    gs_handler.can_on = 1U;         /* can0_config() 里已经开好了，CAN 是在跑的 */

    udev->dev.class_data[GSUSB_CLASS_IDX] = (void *)&gs_handler;

    /* 端点：收的是端点描述符指针；两个 OUT 方向都要自己重新武装 */
    (void)usbd_ep_setup(udev, &(gsusb_config_desc.ep_in));
    (void)usbd_ep_setup(udev, &(gsusb_config_desc.ep_out));
    gs_out_rearm(udev);

    return USBD_OK;
}

/*!
    \brief      反初始化（USB 断开 / 重新枚举时被调用）
*/
static uint8_t gsusb_class_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;

    (void)usbd_ep_clear(udev, GSUSB_IN_EP);
    (void)usbd_ep_clear(udev, GSUSB_OUT_EP);

    return USBD_OK;
}

/*!
    \brief      处理控制请求
    \note       三类请求都进这里：
                  · 厂商码 GSUSB_VENDOR_CODE + wIndex 0x0004 / 0x0005 → WCID 描述符
                  · 字符串索引 0xEE 的 GET_DESCRIPTOR → MS OS 字符串描述符
                    （这一条是从 usbd_enum_ucan.c 里转发过来的）
                  · gs_usb 的 bRequest 0..7
                ⚠ 这个函数会在**枚举还没完成**（USBD_DEFAULT / ADDRESSED）时被调用，
                 所以里面不许依赖"已经 CONFIGURED"，也不许碰 CAN。
                ⚠ 句柄 gs 也可能还是 NULL（它要等 SET_CONFIGURATION 才挂上），
                 所以第 1、2 两块 WCID 只用静态数组、不碰 gs —— Windows 完全
                 可能在 SetConfiguration 之前就来要 0xEE，那时候要是提前返回，
                 主机就把"这台设备没有 OS 描述符"永久记进 usbflags 了。
*/
static uint8_t gsusb_class_req(usb_dev *udev, usb_req *req)
{
    gs_usb_handler *gs = gs_get();

    usb_transc *transc_in = &udev->dev.transc_in[0];
    usb_transc *transc_out = &udev->dev.transc_out[0];

    const uint8_t *desc = NULL;
    uint16_t desc_len = 0U;

    /* --- 1. Windows 要的 MS OS 字符串描述符（索引 0xEE）--- */
    if ((USB_GET_DESCRIPTOR == req->bRequest) &&
        ((uint8_t)(req->wValue >> 8) == USB_DESCTYPE_STR) &&
        (0xEEU == (uint8_t)(req->wValue & 0x00FFU))) {
        desc = gsusb_ms_os_string;
        desc_len = (uint16_t)sizeof(gsusb_ms_os_string);
    }
    /* --- 2. Windows 要的 WCID 功能描述符（厂商码 0x20）--- */
    else if (GSUSB_VENDOR_CODE == req->bRequest) {
        switch (req->wIndex) {
        case 0x0004U:
            desc = gsusb_ms_comp_id;
            desc_len = (uint16_t)sizeof(gsusb_ms_comp_id);
            break;

        case 0x0005U:
            if (0U == req->wValue) {      /* 只给 0 号接口报 GUID（和 candleLight 一致） */
                desc = gsusb_ms_ext_prop;
                desc_len = (uint16_t)sizeof(gsusb_ms_ext_prop);
            }
            break;

        default:
            break;
        }
    }
    /* --- 3. gs_usb 自己的控制请求 --- */
    else {
        /* 下面这些都要用 gs（ts_buf / ep0_out_buf / req_cmd），所以句柄判断
           放在这里，不能提到函数开头 —— 提上去就等于把 0xEE 那两条 WCID 路径
           一起挡在门外了。 */
        if (NULL == gs) {
            return USBD_FAIL;
        }

        switch (req->bRequest) {
        case GS_USB_BREQ_BT_CONST:
            desc = (const uint8_t *)&gsusb_bt_const;
            desc_len = (uint16_t)sizeof(gsusb_bt_const);
            break;

        case GS_USB_BREQ_DEVICE_CONFIG:
            desc = (const uint8_t *)&gsusb_device_config;
            desc_len = (uint16_t)sizeof(gsusb_device_config);
            break;

        case GS_USB_BREQ_TIMESTAMP:
            /* 4 字节微秒计数：SOF 里每毫秒加 1000。
               我们没报 HW_TIMESTAMP 功能位，主机不会把这个数当帧时间戳用，
               只是有的上位机拿它当心跳看。 */
            gs_u32_put(gs->ts_buf, gs->timestamp_us);
            transc_in->xfer_buf = gs->ts_buf;
            transc_in->remain_len = 4U;
            return USBD_OK;

        case GS_USB_BREQ_HOST_FORMAT:   /* 4 字节字节序说明：candleLight 恒小端，收下不管 */
        case GS_USB_BREQ_BITTIMING:     /* 20 字节 */
        case GS_USB_BREQ_MODE:          /* 8 字节 */
        case GS_USB_BREQ_IDENTIFY:      /* 4 字节，收下不管 */
            /* 主机 → 设备：把接收缓冲挂到 EP0，等数据到了 ctlx_out 再处理。
               ⚠ ep0_out_buf 必须 ≥ 64 字节：EP0 的 OUT 长度永远按 64 准备。 */
            gs->req_cmd = req->bRequest;
            transc_out->xfer_buf = gs->ep0_out_buf;
            transc_out->remain_len = req->wLength;
            return USBD_OK;

        default:
            /* 不认识的请求直接 STALL，让主机知道。 */
            return USBD_FAIL;
        }
    }

    if (NULL == desc) {
        return USBD_FAIL;
    }

    /* 设备 → 主机。按主机要的长度裁一下：它要多少给多少，别发多余的。 */
    if (desc_len > req->wLength) {
        desc_len = req->wLength;
    }

    transc_in->xfer_buf = (uint8_t *)desc;
    transc_in->remain_len = desc_len;

    return USBD_OK;
}

/*!
    \brief      控制传输的 OUT 数据到了（前面 req_proc 里挂好的那个缓冲）
    \note       只把值记下来，真正的 CAN 动作留给 gsusb_task()。
*/
static uint8_t gsusb_class_ctlx_out(usb_dev *udev)
{
    gs_usb_handler *gs = gs_get();

    uint16_t count = 0U;
    uint32_t mode = 0U;
    uint32_t feature = 0U;

    if (NULL == gs) {
        return USBD_OK;
    }

    count = usbd_rxcount_get((usb_core_driver *)udev, 0U);

    switch (gs->req_cmd) {
    case GS_USB_BREQ_BITTIMING:
        if (count >= 20U) {
            /* 一定要 memcpy：usb_req 是 #pragma pack(1) 的，直接强转成 u32* 读
               可能未对齐；而且主机下次请求会覆盖这个缓冲。 */
            gs->bittiming[0] = gs_u32_get(&gs->ep0_out_buf[0]);    /* prop_seg    */
            gs->bittiming[1] = gs_u32_get(&gs->ep0_out_buf[4]);    /* phase_seg1  */
            gs->bittiming[2] = gs_u32_get(&gs->ep0_out_buf[8]);    /* phase_seg2  */
            gs->bittiming[3] = gs_u32_get(&gs->ep0_out_buf[12]);   /* sjw         */
            gs->bittiming[4] = gs_u32_get(&gs->ep0_out_buf[16]);   /* brp         */
        }
        break;

    case GS_USB_BREQ_MODE:
        if (count >= 8U) {
            mode = gs_u32_get(&gs->ep0_out_buf[0]);
            feature = gs_u32_get(&gs->ep0_out_buf[4]);

            /* 只认 RESET / START，别的一律当 RESET */
            gs->pending_mode = (GS_CAN_MODE_START == mode) ? GS_CAN_MODE_START : GS_CAN_MODE_RESET;
            gs->feature = feature;
        }
        break;

    case GS_USB_BREQ_HOST_FORMAT:
    case GS_USB_BREQ_IDENTIFY:
    default:
        /* 收下不管 */
        break;
    }

    gs->req_cmd = 0xFFU;

    return USBD_OK;
}

/*!
    \brief      IN 传输完成（一帧已经发上去了）
*/
static uint8_t gsusb_class_in(usb_dev *udev, uint8_t ep_num)
{
    gs_usb_handler *gs = gs_get();

    usb_transc *transc = &udev->dev.transc_in[EP_ID(ep_num)];

    if (NULL == gs) {
        return USBD_OK;
    }

    /* ep_num 是端点号（EP1_IN 就是 1），不是 0x81 */
    if (EP_ID(GSUSB_IN_EP) != ep_num) {
        return USBD_OK;
    }

    /* 整包长度要补一个 ZLP 才表示"这一帧发完了"。
       我们的帧固定 20 字节，永远不会走到这个分支，留着是为了照抄 CDC 的写法、
       以后要发别的长度时不至于踩坑。 */
    if ((0U != transc->xfer_len) && (0U == (transc->xfer_len % transc->max_len))) {
        (void)usbd_ep_send(udev, GSUSB_IN_EP, NULL, 0U);
    } else {
        gs->tx_busy = 0U;
    }

    return USBD_OK;
}

/*!
    \brief      OUT 收到一包（主机发来的一帧）
    \note       ⚠ 必须在中断里把数据 memcpy 走，而且不能碰 CAN 寄存器。
*/
static uint8_t gsusb_class_out(usb_dev *udev, uint8_t ep_num)
{
    gs_usb_handler *gs = gs_get();

    uint16_t count = 0U;

    if (NULL == gs) {
        return USBD_OK;
    }

    (void)ep_num;

    /* ⚠ 用 usbd_rxcount_get()，不要用 transc->xfer_buf：OUT 中断里
       drv_usbd_int.c 会自己把 xfer_buf 往前推（xfer_buf += bcount），
       那个指针已经不是缓冲开头了。长度同理，从 xfer_count 拿。 */
    count = usbd_rxcount_get((usb_core_driver *)udev, EP_ID(GSUSB_OUT_EP));

    if (GSUSB_FRAME_LEN == count) {
        uint8_t next = (uint8_t)((gs->fh_head + 1U) % GSUSB_FH_DEPTH);

        if (next != gs->fh_tail) {
            memcpy(gs->from_host[gs->fh_head], gs->ep_out_buf, GSUSB_FRAME_LEN);
            gs->fh_head = next;
        } else {
            gs->fh_drop++;      /* 队列满：主循环没跟上，这一帧丢掉（不阻塞 USB）*/
        }
    } else if (count > GSUSB_FRAME_LEN) {
        /* 一个包塞了多帧（主机开了 PAD_PKTS 之类）：我们没报那个功能位，
           正常不会出现。只认第一帧，剩下的丢掉，避免解析出一堆错位的东西。 */
        uint8_t next = (uint8_t)((gs->fh_head + 1U) % GSUSB_FH_DEPTH);

        if (next != gs->fh_tail) {
            memcpy(gs->from_host[gs->fh_head], gs->ep_out_buf, GSUSB_FRAME_LEN);
            gs->fh_head = next;
        }
    } else {
        /* 长度不够 20：不是合法的 gs_host_frame，丢掉（主机自己会超时重试） */
    }

    /* 立刻重新武装，不然只能收一帧 */
    gs_out_rearm(udev);

    return USBD_OK;
}

/*!
    \brief      SOF（每 1 ms 一次，驱动里 GINTEN_SOFIE 是开着的）
    \note       用来做微秒级的时间戳计数：主机读 TIMESTAMP 时给它一个在走的数。
*/
static uint8_t gsusb_class_sof(usb_dev *udev)
{
    gs_usb_handler *gs = gs_get();

    if (NULL != gs) {
        gs->timestamp_us += 1000U;
    }

    return USBD_OK;
}

/* ------------------------------------------------------------------------ */
/* 对外接口                                                                  */
/* ------------------------------------------------------------------------ */

void gsusb_init(void)
{
    gs_usb_handler *gs = gs_get();

    if (NULL == gs) {
        return;
    }

    gs->pending_mode = GSUSB_NO_PENDING;
    gs->feature = 0U;
    gs->can_on = 1U;        /* main.c 里已经调过 can0_config()，CAN 是在跑的 */
}

/*!
    \brief      主机 → CAN 的一帧
    \note       成功投进邮箱才出队并 echo；投递失败就原地不动，下一轮再试
                （candleLight 也是把帧放回队头重试：宁可堵着，不丢帧）。
                所以"总线没人应答"时，主机看到的现象是前几帧被 echo、
                后面的帧一直 pending —— 这是真实现象，不是板子坏了。
*/
static void gs_from_host_process(gs_usb_handler *gs, uint8_t *moved)
{
    const uint8_t *f;
    uint32_t can_id;
    uint32_t id;
    uint8_t dlc;
    uint8_t is_ext;
    uint8_t is_rtr;
    uint8_t state;

    if (gs->fh_tail == gs->fh_head) {
        return;
    }

    f = gs->from_host[gs->fh_tail];

    can_id = gs_u32_get(&f[4]);
    dlc = f[8];

    /* 本板只有一路 CAN（channel 0），DLC 只能 0..8；其余的一律丢弃 */
    if ((dlc > 8U) || (0U != f[9])) {
        gs->fh_tail = (uint8_t)((gs->fh_tail + 1U) % GSUSB_FH_DEPTH);
        return;
    }

    is_ext = (0U != (can_id & CAN_EFF_FLAG)) ? 1U : 0U;
    is_rtr = (0U != (can_id & CAN_RTR_FLAG)) ? 1U : 0U;
    id = can_id & (is_ext ? 0x1FFFFFFFU : 0x7FFU);

    state = can0_send_frame_ex(id, is_ext, is_rtr, (uint8_t *)&f[12], dlc);

    if (CAN_TRANSMIT_OK == state) {
        /* echo：把主机发来的这一帧原样送回（echo_id 保留）。
           主机靠 echo 把"已发送"的帧从发送队列里摘掉，不回它就一直 pending。
           candleLight 也是投给 CAN 之后立刻 echo，不等真上总线。 */
        uint8_t next = (uint8_t)((gs->th_head + 1U) % GSUSB_TH_DEPTH);

        if (next != gs->th_tail) {
            memcpy(gs->to_host[gs->th_head], f, GSUSB_FRAME_LEN);
            gs->th_head = next;
        } else {
            gs->th_drop++;
        }

        gs->fh_tail = (uint8_t)((gs->fh_tail + 1U) % GSUSB_FH_DEPTH);
        *moved = 1U;
    }
    /* else：三个邮箱都占着（多半是对面不应答、硬件在无限重发），
             不出队、不 echo，下一轮重试 */
}

/*!
    \brief      把 CAN 收到的帧组包（20 字节 gs_host_frame）推进上行队列
*/
static void gs_can_frame_push(gs_usb_handler *gs, const can_receive_message_struct *rx)
{
    uint8_t  frame[GSUSB_FRAME_LEN];
    uint32_t can_id;
    uint8_t  i;
    uint8_t  next;

    gs_u32_put(&frame[0], GSUSB_ECHO_ID_RX);    /* 总线上来的帧：不是 echo */

    if (CAN_FF_EXTENDED == rx->rx_ff) {
        can_id = (rx->rx_efid & 0x1FFFFFFFU) | CAN_EFF_FLAG;
    } else {
        can_id = rx->rx_sfid & 0x7FFU;
    }
    if (CAN_FT_REMOTE == rx->rx_ft) {
        can_id |= CAN_RTR_FLAG;
    }
    gs_u32_put(&frame[4], can_id);

    frame[8]  = rx->rx_dlen;    /* dlc */
    frame[9]  = 0U;             /* channel：本板只有一路 */
    frame[10] = 0U;             /* flags：没有 CAN-FD、没有硬件时间戳 */
    frame[11] = 0U;             /* reserved */

    for (i = 0U; i < 8U; i++) {
        /* 远程帧没有数据段，rx_data 里是什么都别往外报，直接给 0 */
        frame[12U + i] = (CAN_FT_REMOTE == rx->rx_ft) ? 0U : rx->rx_data[i];
    }

    next = (uint8_t)((gs->th_head + 1U) % GSUSB_TH_DEPTH);

    if (next != gs->th_tail) {
        memcpy(gs->to_host[gs->th_head], frame, GSUSB_FRAME_LEN);
        gs->th_head = next;
    } else {
        gs->th_drop++;          /* 上行堵住了（主机不读），丢帧不阻塞 */
    }
}

void gsusb_task(void)
{
    gs_usb_handler *gs = gs_get();

    can_receive_message_struct rx_msg;
    uint8_t moved = 0U;
    uint8_t i;

    if (NULL == gs) {
        return;
    }

    /* ---- 1. 主机下发的 MODE：只有这里才碰 CAN 寄存器 ---- */
    if (GSUSB_NO_PENDING != gs->pending_mode) {
        uint8_t mode = gs->pending_mode;

        gs->pending_mode = GSUSB_NO_PENDING;

        if (GS_CAN_MODE_START != mode) {
            /* MODE RESET：进初始化模式，收发都停，滤波器留着 */
            can0_stop();
            gs->can_on = 0U;
        } else {
            /* gs_usb 的 bittiming 是"时间量"，GD32 的寄存器字段要 -1；
               brp 是例外：库内部自己 -1，所以原样给。
                 tseg1 = prop_seg + phase_seg1 - 1
                 tseg2 = phase_seg2 - 1
                 sjw   = sjw - 1  */
            uint32_t prop_phase1 = gs->bittiming[0] + gs->bittiming[1];
            uint32_t phase2 = gs->bittiming[2];
            uint32_t sjw = gs->bittiming[3];
            uint32_t brp = gs->bittiming[4];

            /* 功能位：只认我们报过的三个，别的位一律忽略（不 STALL）。
               candleLight 在非 CAN-FD 版本里也是 can_check_feature_ok() 恒真，
               即"不认识的位当没看见"。 */
            uint8_t listen_only = (0U != (gs->feature & GS_CAN_FEATURE_LISTEN_ONLY)) ? 1U : 0U;
            uint8_t loopback = (0U != (gs->feature & GS_CAN_FEATURE_LOOP_BACK)) ? 1U : 0U;
            uint8_t one_shot = (0U != (gs->feature & GS_CAN_FEATURE_ONE_SHOT)) ? 1U : 0U;

            if ((prop_phase1 >= 1U) && (phase2 >= 1U) && (sjw >= 1U) && (prop_phase1 <= 16U)) {
                if (SUCCESS == can0_reconfigure(brp, prop_phase1 - 1U, phase2 - 1U, sjw - 1U,
                                                listen_only, loopback, one_shot)) {
                    gs->can_on = 1U;
                }
            }
            /* 参数不合法就不开：主机那边会因为收不到 echo 而超时。
               这时候在 Watch 窗口看 gs->bittiming[] 的实际值，八成是
               换算或者 BT_CONST 报的取值范围对不上。 */
        }
    }

    /* ---- 2. 主机 → CAN（含 echo）---- */
    gs_from_host_process(gs, &moved);

    /* ---- 3. CAN → 主机：把 FIFO0 取空（硬件只有 3 级，取慢了就丢） ---- */
    for (i = 0U; i < 4U; i++) {
        if (0U == can0_recv_msg(&rx_msg)) {
            break;
        }

        gs_can_frame_push(gs, &rx_msg);
        moved = 1U;
    }

    /* ---- 4. 上行发送：一次只允许一个 IN 传输在飞 ---- */
    if ((0U == gs->tx_busy) && (gs->th_tail != gs->th_head)) {
        memcpy(gs->tx_buf, gs->to_host[gs->th_tail], GSUSB_FRAME_LEN);
        gs->th_tail = (uint8_t)((gs->th_tail + 1U) % GSUSB_TH_DEPTH);

        gs->tx_busy = 1U;
        /* ⚠ usbd_ep_send 只存指针不拷数据（真正拷贝在 TXFE 中断里），
           所以 tx_buf 是句柄成员而不是局部变量；
           而且只能在主循环里调，不能在 USB 中断里调。 */
        (void)usbd_ep_send(&cdc_acm, GSUSB_IN_EP, gs->tx_buf, GSUSB_FRAME_LEN);

        moved = 1U;
    }

    /* ---- 5. 灯 ---- */
    if (0U != moved) {
        led_progress();     /* 传输中：两两交替闪 */
    } else {
        led_water();        /* 空闲：走马灯 */
    }
}
