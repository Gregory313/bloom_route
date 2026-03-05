#ifndef CONTROL_H
#define CONTROL_H

#include "conf.h"

void send_COMMs(void);
void periodic_handler(int _events, int _fd);
void receive_packet_and_handle(const uint8_t* packet, size_t length, const Address* src_addr);
void send_data_packet(uint32_t dst_id, const uint8_t* payload, size_t payload_length);
void send_COMMs_to_neighbors(const void *data, size_t length);
int initialize_node(void);

#endif