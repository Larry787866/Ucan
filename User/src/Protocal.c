    /*!
        \file    Protocal.c
        \brief   USB(CDC) <-> CAN0 转发协议（Ucan）

        \version 2026-9-22

        一帧固定 18 字节，沿用原工程的格式，PC 上位机不用改：

        偏移  长度  内容
        [0]    1    0xAA           帧头 1
        [1]    1    0x55           帧头 2
        [2]    1    0x01 / 0x02    01 = 标准帧, 02 = 扩展帧
        [3]    4    CAN ID         大端；标准帧只取低 11 位
        [7]    1    DLC            0 ~ 8
        [8]    8    DATA           不足 8 字节的高位补 0
        [16]   1    SUM            [2]~[15] 逐字节相加取低 8 位
        [17]   1    0xEE           帧尾
    */

    #include "Protocal.h"
    #include "can.h"
    #include "gd32f30x_can.h"
    #include "systick.h"
    #include "usbd_core.h"
    #include "cdc_acm_core.h"

    #include <string.h>

    /* ============================ 参数 ============================ */

    #define PKT_SOF1            0xAAU
    #define PKT_SOF2            0x55U
    #define PKT_EOF             0xEEU
    #define PKT_TOTAL_LEN       18U
    #define PKT_TYPE_STD        0x01U
    #define PKT_TYPE_EXT        0x02U

    #define USB_RX_RING_SIZE    512U        /* 电脑下发字节的环形缓冲 */
    #define CAN_UP_QUEUE_SIZE   16U         /* CAN 上行帧队列深度 */

    #define PROTO_ACTIVE_MS     300U        /* 这段时间内有数据就算"正在传" */
    #define PROTO_DONE_MS       1300U       /* 传完之后 complete 指示保持多久 */

    /* main.c 里的 USB 设备句柄 */
    extern usb_core_driver cdc_acm;

    /* ======================== 内部数据结构 ======================== */

    /* 电脑下发：裸字节环形缓冲 */
    typedef struct
    {
        uint8_t  buf[USB_RX_RING_SIZE];
        uint16_t head;
        uint16_t tail;
    } usb_rx_ring_t;

    static usb_rx_ring_t usb_rx_ring;

    /* CAN 上行：一帧的信息 */
    typedef struct
    {
        uint32_t id;
        uint8_t  is_extended;
        uint8_t  len;
        uint8_t  data[8];
    } can_up_frame_t;

    static can_up_frame_t    can_up_queue[CAN_UP_QUEUE_SIZE];
    static volatile uint16_t can_up_head = 0U;      /* 写入位置 */
    static volatile uint16_t can_up_tail = 0U;      /* 读出位置 */

    /* 解包状态机 */
    static uint8_t rx_pkt[PKT_TOTAL_LEN];
    static uint8_t rx_idx = 0U;
    static uint8_t rx_state = 0U;                   /* 0=找 AA  1=找 55  2=收正文  3=等 EE */

    /* 数据传输状态（点灯用） */
    static volatile uint32_t proto_last_active = 0U;    /* 最近一次有数据的时刻 */
    static volatile uint8_t  proto_ever_active = 0U;    /* 是否传过（区分上电空闲） */

    /* CAN 接收计数（调试用，Keil 的 Watch 窗口里看这两个） */
    volatile uint32_t can_rx_count   = 0U;              /* 总共收到过多少帧 */
    volatile uint32_t can_rx_last_id = 0U;               /* 最后一帧的 ID */

    /* ======================== 数据传输状态（点灯用） ======================== */

    /*!
        \brief      有数据流动时打个时间戳
    */
    static void proto_touch(void)
    {
        proto_last_active = get_systick_tick();
        proto_ever_active = 1U;
    }

    /* ============================ 工具函数 ============================ */

    /*!
        \brief      确认 USB 已枚举完成
        \retval     1=可以收发  0=还没枚举完
    */
    static uint8_t usb_ready(void)
    {
        return (USBD_CONFIGURED == cdc_acm.dev.cur_status) ? 1U : 0U;
    }

    /*!
        \brief      取 CDC 数据接口的句柄
        \retval     句柄，未初始化时返回 NULL
    */
    static usb_cdc_handler *cdc_handler_get(void)
    {
        if (NULL == cdc_acm.dev.class_data[CDC_COM_INTERFACE])
        {
            return NULL;
        }

        return (usb_cdc_handler *)cdc_acm.dev.class_data[CDC_COM_INTERFACE];
    }

    /*!
        \brief      从电脑取一包数据
        \param[in]  buf: 存放数据的缓冲
        \param[in]  max_len: 缓冲长度
        \retval     实际取到的字节数，0 表示这一轮没有新数据
        \note       CDC 只有一个 64 字节的接收缓冲，所以取完必须马上重新武装
                    OUT 端点，本函数内部已经做了。
    */
    static uint16_t usb_recv(uint8_t *buf, uint16_t max_len)
    {
        usb_cdc_handler *cdc = cdc_handler_get();
        uint16_t n = 0U;

        if ((NULL == cdc) || (NULL == buf))
        {
            return 0U;
        }

        if ((1U == cdc->packet_receive) && (0U != cdc->receive_length))
        {
            n = (cdc->receive_length > (uint32_t)max_len) ? max_len : (uint16_t)cdc->receive_length;
            memcpy(buf, cdc->data, (uint32_t)n);
        }

        if (1U == cdc->packet_receive)
        {
            /* 重新武装 OUT 端点。这里不能用 cdc_acm_data_receive()：
            它会把 packet_sent 一起清零，而 packet_sent 是上行发送的完成标志，
            抹掉之后就再也没人把它置回 1，CAN->USB 就发不出去了。 */
            cdc->packet_receive = 0U;
            cdc->receive_length = 0U;
            usbd_ep_recev(&cdc_acm, CDC_DATA_OUT_EP, cdc->data, USB_CDC_DATA_PACKET_SIZE);
        }

        return n;
    }

    /*!
        \brief      上一包数据发完了没有
        \retval     1=还在发  0=可以发下一包
    */
    static uint8_t usb_tx_busy(void)
    {
        usb_cdc_handler *cdc = cdc_handler_get();

        if (NULL == cdc)
        {
            return 1U;
        }

        return (0U == cdc->packet_sent) ? 1U : 0U;
    }

    /*!
        \brief      往电脑发一包数据
        \param[in]  data: 数据指针，必须是 static/全局的（发送是异步的）
        \param[in]  len: 数据长度
        \note       上一包还没发完就直接丢掉本包，不阻塞主循环。
    */
    static void usb_send(uint8_t *data, uint16_t len)
    {
        usb_cdc_handler *cdc = cdc_handler_get();

        if ((NULL == cdc) || (NULL == data) || (0U == len))
        {
            return;
        }

        if (0U == cdc->packet_sent)
        {
            return;                                 /* 上一包还在路上 */
        }

        cdc->packet_sent = 0U;
        usbd_ep_send(&cdc_acm, CDC_DATA_IN_EP, data, (uint32_t)len);
    }

    /* ======================== 方向 1：电脑 -> CAN ======================== */

    /*!
        \brief      把收到的字节塞进环形缓冲
        \param[in]  buf: 数据
        \param[in]  len: 长度
    */
    static void usb_rx_feed(uint8_t *buf, uint32_t len)
    {
        uint32_t i = 0U;
        uint16_t next = 0U;

        for (i = 0U; i < len; i++)
        {
            next = (uint16_t)((usb_rx_ring.head + 1U) % USB_RX_RING_SIZE);
            if (next == usb_rx_ring.tail)
            {
                break;                              /* 缓冲满，丢掉剩下的字节 */
            }

            usb_rx_ring.buf[usb_rx_ring.head] = buf[i];
            usb_rx_ring.head = next;
        }
    }

    /*!
        \brief      从环形缓冲取一个字节
        \param[out] byte: 取到的字节
        \retval     1=取到  0=缓冲是空的
    */
    static uint8_t usb_rx_get(uint8_t *byte)
    {
        if (usb_rx_ring.head == usb_rx_ring.tail)
        {
            return 0U;
        }

        *byte = usb_rx_ring.buf[usb_rx_ring.tail];
        usb_rx_ring.tail = (uint16_t)((usb_rx_ring.tail + 1U) % USB_RX_RING_SIZE);

        return 1U;
    }

    /*!
        \brief      把环形缓冲里的字节解成一帧帧发给 CAN
    */
    static void usb_to_can_process(void)
    {
        uint8_t  ch = 0U;
        uint8_t  sum = 0U;
        uint8_t  dlc = 0U;
        uint8_t  i = 0U;
        uint32_t can_id = 0U;

        while (0U != usb_rx_get(&ch))
        {
            switch (rx_state)
            {
            case 0U:                                    /* 等 0xAA */
                if (PKT_SOF1 == ch)
                {
                    rx_pkt[0] = ch;
                    rx_state = 1U;
                }
                break;

            case 1U:                                    /* 等 0x55 */
                if (PKT_SOF2 == ch)
                {
                    rx_pkt[1] = ch;
                    rx_idx = 2U;
                    rx_state = 2U;
                }
                else if (PKT_SOF1 != ch)
                {
                    rx_state = 0U;                      /* 帧头对不上，重来 */
                }                                       /* 又是 0xAA 就继续等 0x55 */
                break;

            case 2U:                                    /* 收 [2]~[16] */
                rx_pkt[rx_idx] = ch;
                rx_idx++;
                if (rx_idx >= (PKT_TOTAL_LEN - 1U))
                {
                    rx_state = 3U;
                }
                break;

            case 3U:                                    /* 等 0xEE 并校验 */
                if (PKT_EOF == ch)
                {
                    rx_pkt[PKT_TOTAL_LEN - 1U] = ch;

                    sum = 0U;
                    for (i = 2U; i <= 15U; i++)
                    {
                        sum = (uint8_t)(sum + rx_pkt[i]);
                    }

                    if (sum == rx_pkt[16])
                    {
                        can_id = ((uint32_t)rx_pkt[3] << 24) |
                                ((uint32_t)rx_pkt[4] << 16) |
                                ((uint32_t)rx_pkt[5] << 8)  |
                                ((uint32_t)rx_pkt[6]);

                        dlc = rx_pkt[7];
                        if (dlc > 8U)
                        {
                            dlc = 8U;
                        }

                        can0_send_frame(can_id,
                                        (PKT_TYPE_EXT == rx_pkt[2]) ? 1U : 0U,
                                        &rx_pkt[8],
                                        dlc);
                    }
                }

                rx_state = 0U;                          /* 不管对错都重新找帧头 */
                break;

            default:
                rx_state = 0U;
                break;
            }
        }
    }

    /* ======================== 方向 2：CAN -> 电脑 ======================== */

    /*!
        \brief      把 CAN 收到的一帧放进上行队列
        \param[in]  can_rx: CAN 接收结构
        \note       队列满就丢帧（USB 侧堵住了）。
    */
    static void can_up_push(can_receive_message_struct *can_rx)
    {
        uint16_t next = 0U;
        uint8_t i = 0U;

        next = (uint16_t)((can_up_head + 1U) % CAN_UP_QUEUE_SIZE);
        if (next == can_up_tail)
        {
            return;                                 /* 队列满 */
        }

        if (CAN_FF_STANDARD == can_rx->rx_ff)
        {
            can_up_queue[can_up_head].id = can_rx->rx_sfid;
            can_up_queue[can_up_head].is_extended = 0U;
        }
        else
        {
            can_up_queue[can_up_head].id = can_rx->rx_efid;
            can_up_queue[can_up_head].is_extended = 1U;
        }

        if (can_rx->rx_dlen > 8U)
        {
            can_up_queue[can_up_head].len = 8U;
        }
        else
        {
            can_up_queue[can_up_head].len = can_rx->rx_dlen;
        }

        for (i = 0U; i < 8U; i++)
        {
            can_up_queue[can_up_head].data[i] = can_rx->rx_data[i];
        }

        can_up_head = next;
    }

    /*!
        \brief      把 CAN0 FIFO0 里的报文全部取出来
        \note       主循环里轮询。FIFO0 只有 3 级深，所以本函数要经常被调用；
                    以后想改成中断方式，就在 CAN0_RX0_IRQHandler 里
                    while(can0_recv_msg(&rx_msg)) can_up_push(&rx_msg)，
                    同时把 protocol_task() 里的 can_rx_poll() 去掉。
    */
    static void can_rx_poll(void)
    {
        can_receive_message_struct rx_msg;

        while (0U != can0_recv_msg(&rx_msg))
        {
            if (CAN_FT_DATA != rx_msg.rx_ft)
            {
                continue;                           /* 远程帧不往上传 */
            }

            can_up_push(&rx_msg);
            proto_touch();                          /* CAN 收到数据 */
        }
    }

    /* ======================== CAN 接收自测（调试用） ======================== */

    /*!
        \brief      把 CAN0 FIFO0 里的报文全部取出来，同时记下收到过多少帧
        \note       和 can_rx_poll() 干的是同一件事，只是多记了两个数：
                    can_rx_count   收到过多少帧
                    can_rx_last_id 最后一帧的 ID
                    在 Keil 的 Watch 窗口里加上这两个变量，跑起来，
                    让 Cangaroo 发一帧，can_rx_count 就应该 +1。

                    一直是 0 = 板子物理上一个字节都没收到（收发器 / PB8 /
                    接线那一段），不用再怀疑软件了。

                    主循环里在 protocol_task() 之前调用：它会先把 FIFO0 掏空，
                    所以这一轮 protocol_task() 里的 can_rx_poll() 拿不到帧；
                    帧还是照常进了上行队列，protocol_task() 里的 can_up_flush()
                    会把它发给电脑，VOFA 和灯都和平时一样有反应。
    */
    void can_rx_test(void)
    {
        can_receive_message_struct rx_msg;

        while (0U != can0_recv_msg(&rx_msg))
        {
            can_rx_count++;

            if (CAN_FF_STANDARD == rx_msg.rx_ff)
            {
                can_rx_last_id = rx_msg.rx_sfid;
            }
            else
            {
                can_rx_last_id = rx_msg.rx_efid;
            }

            if (CAN_FT_DATA != rx_msg.rx_ft)
            {
                continue;                           /* 远程帧不往上传 */
            }

            can_up_push(&rx_msg);
            proto_touch();                          /* CAN 收到数据，点灯 */
        }
    }

    /*!
        \brief      把上行队列里的帧组包发给电脑
    */
    static void can_up_flush(void)
    {
        /* 必须是 static：usbd_ep_send() 是异步的，函数返回时数据还没搬完 */
        static uint8_t tx_pkt[PKT_TOTAL_LEN];

        uint32_t id = 0U;
        uint8_t  sum = 0U;
        uint8_t  i = 0U;
        uint8_t  len = 0U;

        while (can_up_tail != can_up_head)
        {
            if (0U == usb_ready())
            {
                break;                              /* 还没枚举完，下一轮再说 */
            }

            /* 必须先判忙再组包：usbd_ep_send() 只是把 tx_pkt 的地址交给 USB 核心，
            真正的数据是在 IN 中断里才从 tx_pkt 搬进 FIFO 的
            （drv_usbd_int.c: usb_txfifo_write(... transc->xfer_buf ...)），
            所以上一包没走完之前绝不能动 tx_pkt。 */
            if (0U != usb_tx_busy())
            {
                break;                              /* 上一包还在路上，这一轮不碰缓冲 */
            }

            len = can_up_queue[can_up_tail].len;
            id  = can_up_queue[can_up_tail].id;

            tx_pkt[0] = PKT_SOF1;
            tx_pkt[1] = PKT_SOF2;
            tx_pkt[2] = (0U != can_up_queue[can_up_tail].is_extended) ? PKT_TYPE_EXT : PKT_TYPE_STD;
            tx_pkt[3] = (uint8_t)(id >> 24);
            tx_pkt[4] = (uint8_t)(id >> 16);
            tx_pkt[5] = (uint8_t)(id >> 8);
            tx_pkt[6] = (uint8_t)(id);
            tx_pkt[7] = len;

            for (i = 0U; i < 8U; i++)
            {
                tx_pkt[8U + i] = (i < len) ? can_up_queue[can_up_tail].data[i] : 0U;
            }

            sum = 0U;
            for (i = 2U; i <= 15U; i++)
            {
                sum = (uint8_t)(sum + tx_pkt[i]);
            }
            tx_pkt[16] = sum;
            tx_pkt[17] = PKT_EOF;

            usb_send(tx_pkt, PKT_TOTAL_LEN);

            can_up_tail = (uint16_t)((can_up_tail + 1U) % CAN_UP_QUEUE_SIZE);
        }
    }

    /* ======================== 自动测试发送（调试用） ======================== */

    /*!
        \brief      按固定周期自动发一帧测试数据（调试用）
        \note       由 Protocal.h 里的 PROTO_AUTO_TEST 控制：
                    = 0 什么都不做（正式固件就是这个值）
                    = 1 直接灌进上行队列发给 VOFA，完全不碰 CAN
                        （单独验证 USB 上行通路，不需要 Cangaroo 和总线）
                    = 2 走 can0_send_frame() 发到 CAN 总线上
                        （用来数 Cangaroo 里这一帧出现几次）
                    数据固定 8 字节：前两字节是递增序号（大端），
                    后面是 A5 5A 11 22 33 44，标准帧 ID = 0x123。
    */
    static void proto_auto_test(void)
    {
#if (0U != PROTO_AUTO_TEST)
        static uint32_t last = 0U;
        static uint16_t seq  = 0U;
        uint8_t payload[8];

        if ((get_systick_tick() - last) < PROTO_AUTO_TEST_MS)
        {
            return;
        }
        last = get_systick_tick();

        /* 前两字节放序号，VOFA 里能看出数据在刷新 */
        payload[0] = (uint8_t)(seq >> 8);
        payload[1] = (uint8_t)(seq);
        payload[2] = 0xA5U;
        payload[3] = 0x5AU;
        payload[4] = 0x11U;
        payload[5] = 0x22U;
        payload[6] = 0x33U;
        payload[7] = 0x44U;

        if (1U == PROTO_AUTO_TEST)
        {
            /* 绕开 CAN：自己造一帧塞进上行队列 */
            can_receive_message_struct fake;

            memset(&fake, 0, sizeof(fake));
            fake.rx_ff   = CAN_FF_STANDARD;
            fake.rx_sfid = 0x123U;
            fake.rx_ft   = CAN_FT_DATA;
            fake.rx_dlen = 8U;
            memcpy(fake.rx_data, payload, 8U);

            can_up_push(&fake);
            proto_touch();
            can_up_flush();
        }
        else if (2U == PROTO_AUTO_TEST)
        {
            (void)can0_send_frame(0x123U, 0U, payload, 8U);
        }

        seq++;
#endif
    }

    /* ============================ 对外接口 ============================ */

    /*!
        \brief      协议层初始化
    */
    void protocol_init(void)
    {
        usb_rx_ring.head = 0U;
        usb_rx_ring.tail = 0U;

        can_up_head = 0U;
        can_up_tail = 0U;

        rx_idx = 0U;
        rx_state = 0U;

        memset(rx_pkt, 0, sizeof(rx_pkt));
    }

    /*!
        \brief      协议层任务，主循环里反复调用
    */
    void protocol_task(void)
    {
        uint8_t  usb_buf[USB_CDC_RX_LEN];
        uint16_t n = 0U;

        if (0U == usb_ready())
        {
            return;                                 /* 还没枚举完 */
        }

        /* 调试用：自动测试发送（PROTO_AUTO_TEST = 0 时不做任何事） */
        proto_auto_test();

        /* 电脑 -> CAN */
        n = usb_recv(usb_buf, (uint16_t)sizeof(usb_buf));
        if (0U != n)
        {
            usb_rx_feed(usb_buf, (uint32_t)n);
            proto_touch();                          /* 电脑发来数据 */
        }
        usb_to_can_process();

        /* CAN -> 电脑 */
        can_rx_poll();
        can_up_flush();
    }

    /*!
        \brief      当前数据传输状态，用来点灯
        \retval     PROTO_IDLE 没有数据传输（走马灯）
                    PROTO_BUSY 正在传（闪烁）
                    PROTO_DONE 刚传完（常亮/快闪一段时间）
        \note       最后一次数据之后 PROTO_ACTIVE_MS 内算 BUSY，
                    再过 PROTO_DONE_MS 之前算 DONE，之后回到 IDLE。
    */
    uint8_t protocol_activity(void)
    {
        uint32_t idle = 0U;

        if (0U == proto_ever_active)
        {
            return PROTO_IDLE;                      /* 上电还没传过 */
        }

        idle = get_systick_tick() - proto_last_active;

        if (idle < PROTO_ACTIVE_MS)
        {
            return PROTO_BUSY;
        }

        if (idle < PROTO_DONE_MS)
        {
            return PROTO_DONE;
        }

        proto_ever_active = 0U;                     /* 指示结束，回到空闲 */

        return PROTO_IDLE;
    }
