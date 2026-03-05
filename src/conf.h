#ifndef CONF_H
#define CONF_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <uthash.h>

#define BLOOM_M 1024
#define BLOOM_K 8
#define BLOOM_C 3
#define TIMEOUT_NEIGHBOR_SEC 60
#define COMM_SEND_INTERVAL_SEC 10
#define MAX_CONTACTS 16

#define MAX(a,b) ((a) > (b) ? (a) : (b))

// Типы пакетов
#define TYPE_COMM 0x01
#define TYPE_DATA 0x02

#define log_debug(...) do { if (0) fprintf(stderr, __VA_ARGS__); } while(0)
#define log_warning(...) fprintf(stderr, "WARNING: " __VA_ARGS__)

typedef struct sockaddr_in Address;

typedef struct {
    uint8_t type;
    uint32_t sender_id;
    uint8_t bloom[BLOOM_M];
} COMM;

typedef struct {
    uint8_t type;
    uint32_t sender_id;
    uint32_t dst_id;
    uint16_t hop_count;
    uint16_t length;
    uint8_t payload[0];
} DATA;

struct Neighbor {
    uint32_t sender_id;
    uint8_t bloom[BLOOM_M];
    Address addr;
    uint64_t last_updated;
    UT_hash_handle hh;
};

typedef struct {
    int udp_socket;
    uint16_t listen_port;
    uint32_t own_id;
    uint64_t time_now;
    int num_initial_contacts;
    Address initial_contacts[MAX_CONTACTS];
} GState;

extern uint8_t g_own_id_bloom[BLOOM_M];
extern uint8_t g_own_bloom[BLOOM_M];
extern struct Neighbor *g_entries;
extern GState gstate;

#endif