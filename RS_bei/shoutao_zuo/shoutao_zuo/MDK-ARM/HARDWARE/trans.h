#ifndef trans_H
#define trans_H

#include "headfile.h"


uint8_t checksum_calculate(uint8_t *data, uint16_t length);
void send_sensor_data(void);

extern uint8_t tx_buffer[88];
#endif 
    