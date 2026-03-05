#include "bloom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void bloom_degrade(uint8_t *new_bloom, const uint8_t *old_bloom) {
    if (!new_bloom || !old_bloom) {
        log_warning("NULL bloom pointer in bloom_degrade");
        return;
    }
    for (size_t i = 0; i < BLOOM_K; i++) {
        const int r = rand() % BLOOM_M;
        if (old_bloom[r] > 0) {
            if (new_bloom[r] > 0) {
                new_bloom[r]--;
            }
        }
    }
}

void bloom_init(uint8_t *bloom, uint32_t id) {
    if (!bloom) {
        log_warning("NULL bloom pointer in bloom_init");
        return;
    }
    memset(bloom, 0, BLOOM_M);
    uint64_t next = (uint64_t)id;
    uint64_t multiplier = 6364136223846793005ULL;
    uint64_t increment = 1442695040888963407ULL;
    for (size_t i = 0; i < BLOOM_K; i++) {
        next = next * multiplier + increment;
        uint32_t hash_val = (uint32_t)(next >> 32);
        bloom[hash_val % BLOOM_M] = BLOOM_C;
    }
}

void bloom_add(uint8_t *bloom1, const uint8_t *bloom2) {
    if (!bloom1 || !bloom2) {
        log_warning("NULL bloom pointer in bloom_add");
        return;
    }
    for (size_t i = 0; i < BLOOM_M; i++) {
        bloom1[i] = MAX(bloom1[i], bloom2[i]);
    }
}

uint32_t bloom_probability(const uint8_t *origin_bloom, const uint8_t *destination_bloom) {
    if (!origin_bloom || !destination_bloom) {
        log_warning("NULL bloom pointer in probability");
        return 0;
    }
    uint32_t prob = 0;
    for (size_t i = 0; i < BLOOM_M; i++) {
        if (destination_bloom[i] > 0) {
            prob += origin_bloom[i];
        }
    }
    return prob;
}

char *str_bloom(char *buf, const uint8_t *bloom) {
    if (!buf) return "(null)";
    if (!bloom) {
        snprintf(buf, BLOOM_M * 6 + 1, "(null bloom)");
        return buf;
    }
    char *cur = buf;
    size_t buf_size = BLOOM_M * 4 + 1;
    int len = 0;
    for (size_t i = 0; i < BLOOM_M; i++) {
        if ((size_t)len < buf_size - 1) {
            if (i == 0) {
                len += snprintf(cur + len, buf_size - len, "%u", (unsigned) bloom[i]);
            } else {
                len += snprintf(cur + len, buf_size - len, " %u", (unsigned) bloom[i]);
            }
        } else {
            break;
        }
    }
    buf[buf_size - 1] = '\0';
    return buf;
}