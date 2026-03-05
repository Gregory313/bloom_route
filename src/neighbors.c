#include "neighbors.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

void neighbor_timeout() {
    struct Neighbor *tmp;
    struct Neighbor *cur;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    gstate.time_now = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    HASH_ITER(hh, g_entries, cur, tmp) {
        if (!cur) continue;
        if ((cur->last_updated + (uint64_t)TIMEOUT_NEIGHBOR_SEC * 1000ULL) < gstate.time_now) {
            HASH_DEL(g_entries, cur);
            free(cur);
        }
    }
}

struct Neighbor *neighbor_find(uint32_t sender_id) {
    struct Neighbor *cur;
    HASH_FIND(hh, g_entries, &sender_id, sizeof(uint32_t), cur);
    return cur;
}

struct Neighbor *neighbor_add(uint32_t sender_id, uint8_t *bloom, const Address *addr) {
    if (!bloom || !addr) {
        log_warning("NULL bloom");
        return NULL;
    }

    struct Neighbor *existing = neighbor_find(sender_id);
    if (existing) {
        log_debug("neighbor 0x%08x from %s already exists, updating...", sender_id, str_addr(addr));
        memcpy(&existing->bloom, bloom, sizeof(existing->bloom));
        memcpy(&existing->addr, addr, sizeof(Address));
        struct timeval tv;
        gettimeofday(&tv, NULL);
        existing->last_updated = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        return existing;
    }

    struct Neighbor *e = (struct Neighbor*) malloc(sizeof(struct Neighbor));
    if (!e) {
        perror("malloc failed in neighbor_add");
        return NULL;
    }
    e->sender_id = sender_id;
    memcpy(&e->bloom, bloom, sizeof(e->bloom));
    memcpy(&e->addr, addr, sizeof(Address));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    e->last_updated = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    HASH_ADD(hh, g_entries, sender_id, sizeof(uint32_t), e);
    log_debug("Added new neighbor 0x%08x from %s", sender_id, str_addr(addr));
    return e;
}