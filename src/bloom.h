#ifndef BLOOM_H
#define BLOOM_H

#include "conf.h"

void bloom_degrade(uint8_t *new_bloom, const uint8_t *old_bloom);
void bloom_init(uint8_t *bloom, uint32_t id);
void bloom_add(uint8_t *bloom1, const uint8_t *bloom2);
uint32_t bloom_probability(const uint8_t *origin_bloom, const uint8_t *destination_bloom);
char *str_bloom(char *buf, const uint8_t *bloom);

#endif