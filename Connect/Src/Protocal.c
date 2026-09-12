#include "Protocal.h"
#include "bsp_usbd.h"

#include <string.h>

#define RING_BUF_SIZE   512U

/* 环形缓冲区 (吸收 USB 粘包、断包) */
typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    uint16_t head;      /* 写指针 */
    uint16_t tail;      /* 读指针 */
} ring_buffer_t;

static ring_buffer_t usb_rx_ring;

void protocol_init(void)
{
    usb_rx_ring.head = 0U;
    usb_rx_ring.tail = 0U;
}

/* 把 USB 收到的裸字节流压入环形缓冲区 */
void protocol_usb_feed(uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (NULL == buf) {
        return;
    }

    for (i = 0U; i < len; i++) {
        uint16_t next_head = (uint16_t)((usb_rx_ring.head + 1U) % RING_BUF_SIZE);

        if (next_head == usb_rx_ring.tail) {
            /* 缓冲区满，丢掉本字节及剩下的，等上层消费完再来 */
            break;
        }

        usb_rx_ring.buf[usb_rx_ring.head] = buf[i];
        usb_rx_ring.head = next_head;
    }
}

/* 从环形缓冲区取一个字节，返回 1 表示取到 */
static uint8_t ring_buffer_read(uint8_t *byte)
{
    if (usb_rx_ring.head == usb_rx_ring.tail) {
        return 0U;      /* 空 */
    }

    *byte = usb_rx_ring.buf[usb_rx_ring.tail];
    usb_rx_ring.tail = (uint16_t)((usb_rx_ring.tail + 1U) % RING_BUF_SIZE);

    return 1U;
}

/* ---------------- 方向 1：电脑 -> CAN 状态机解包 ---------------- */
typedef enum {
    STATE_SOF1 = 0,
    STATE_SOF2,
    STATE_DATA,
    STATE_EOF
} parse_state_t;

static parse_state_t parse_state = STATE_SOF1;
static uint8_t rx_pkt[PKT_TOTAL_LEN];
static uint8_t rx_idx = 0U;

void protocol_usb_to_can_process(void)
{
    uint8_t ch;
    uint8_t calc_sum;
    uint8_t dlc;
    uint8_t i;
    uint32_t can_id;

    while (0U != ring_buffer_read(&ch)) {
        switch (parse_state) {
        case STATE_SOF1:
            if (PKT_SOF1 == ch) {
                rx_pkt[0] = ch;
                parse_state = STATE_SOF2;
            }
            break;

        case STATE_SOF2:
            if (PKT_SOF2 == ch) {
                rx_pkt[1] = ch;
                rx_idx = 2U;
                parse_state = STATE_DATA;
            } else if (PKT_SOF1 == ch) {
                /* 这个字节本身就是下一帧的帧头 1，别浪费 */
                rx_pkt[0] = ch;
            } else {
                parse_state = STATE_SOF1;   /* 帧头匹配失败 */
            }
            break;

        case STATE_DATA:
            rx_pkt[rx_idx++] = ch;
            if (rx_idx >= (PKT_TOTAL_LEN - 1U)) {
                parse_state = STATE_EOF;    /* 校验和已收到，只等帧尾 */
            }
            break;

        case STATE_EOF:
            if (PKT_EOF == ch) {
                rx_pkt[PKT_TOTAL_LEN - 1U] = ch;

                /* 校验和 = [2]~[15] 累加 */
                calc_sum = 0U;
                for (i = 2U; i <= 15U; i++) {
                    calc_sum = (uint8_t)(calc_sum + rx_pkt[i]);
                }

                if (calc_sum == rx_pkt[16]) {
                    can_id = ((uint32_t)rx_pkt[3] << 24) |
                             ((uint32_t)rx_pkt[4] << 16) |
                             ((uint32_t)rx_pkt[5] << 8)  |
                             ((uint32_t)rx_pkt[6]);

                    dlc = rx_pkt[7];
                    if (dlc > 8U) {
                        dlc = 8U;
                    }

                    /* 发射到物理 CAN 总线 */
                    can0_send_frame(can_id,
                                    (PKT_TYPE_EXT == rx_pkt[2]) ? 1U : 0U,
                                    &rx_pkt[8],
                                    dlc);
                }
            }
            parse_state = STATE_SOF1;       /* 无论成功失败，都去找下一帧 */
            break;

        default:
            parse_state = STATE_SOF1;
            break;
        }
    }
}

/* ---------------- 方向 2：CAN -> 电脑 组包发送 ---------------- */
void protocol_can_to_usb_send(can_receive_message_struct *can_rx)
{
    /* 必须是 static：usbd_ep_send() 会保留这个指针给中断里的后续分包用 */
    static uint8_t tx_pkt[PKT_TOTAL_LEN];
    uint32_t id;
    uint8_t sum;
    uint8_t i;

    if ((NULL == can_rx) || (0U == bsp_usbd_is_ready())) {
        return;     /* USB 尚未枚举完成，直接丢 */
    }

    /* 1. 帧头 */
    tx_pkt[0] = PKT_SOF1;
    tx_pkt[1] = PKT_SOF2;

    /* 2. 帧类型 */
    tx_pkt[2] = (CAN_FF_STANDARD == can_rx->rx_ff) ? PKT_TYPE_STD : PKT_TYPE_EXT;

    /* 3. CAN ID（标准帧取 sfid，扩展帧取 efid），大端 */
    id = (CAN_FF_STANDARD == can_rx->rx_ff) ? can_rx->rx_sfid : can_rx->rx_efid;
    tx_pkt[3] = (uint8_t)(id >> 24);
    tx_pkt[4] = (uint8_t)(id >> 16);
    tx_pkt[5] = (uint8_t)(id >> 8);
    tx_pkt[6] = (uint8_t)(id);

    /* 4. 数据长度 */
    tx_pkt[7] = can_rx->rx_dlen;

    /* 5. 数据载荷，不足 8 字节补 0 */
    memset(&tx_pkt[8], 0, 8);
    for (i = 0U; (i < can_rx->rx_dlen) && (i < 8U); i++) {
        tx_pkt[8U + i] = can_rx->rx_data[i];
    }

    /* 6. 校验和 */
    sum = 0U;
    for (i = 2U; i <= 15U; i++) {
        sum = (uint8_t)(sum + tx_pkt[i]);
    }
    tx_pkt[16] = sum;

    /* 7. 帧尾 */
    tx_pkt[17] = PKT_EOF;

    /* 8. 经 USB CDC 虚拟串口送给电脑 */
    bsp_usbd_send(tx_pkt, PKT_TOTAL_LEN);
}
