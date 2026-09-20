#ifndef CAN_H
#define CAN_H

#include "gd32f30x.h"

void can0_config(void);
uint8_t can0_recv_msg(uint32_t *id, uint8_t *data, uint8_t *recv_len);
uint8_t can0_send_msg(uint32_t id, uint8_t *data, uint8_t send_len);

uint8_t can0_send_test(void);
#define CAN_TRANSMIT_TIMEOUT              ((uint8_t)5U)

#endif /* CAN_H */
