#ifndef NEIGHBORS_H
#define NEIGHBORS_H

#include "conf.h"

void neighbor_timeout(void);
struct Neighbor *neighbor_find(uint32_t sender_id);
struct Neighbor *neighbor_add(uint32_t sender_id, uint8_t *bloom, const Address *addr);

#endif