#ifndef UTILS_H
#define UTILS_H

#include "conf.h"

char* str_addr(const Address* addr);
bool address_is_broadcast(const Address* addr);
bool address_equal(const Address* addr1, const Address* addr2);
size_t get_data_size(const DATA *data);
void send_ucast_l2(const Address* dst_addr, const void *data, size_t length);

#endif