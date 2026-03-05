#include "control.h"
#include "bloom.h"
#include "neighbors.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

static void handle_COMM(const Address *rcv, const Address *src,const Address *dst, COMM *p, size_t length) {
    (void)rcv;
    (void)dst;
    if (length != sizeof(COMM)) {
        return;
    }
    if (!p || !src) {
        return;
    }
    if (p->sender_id == gstate.own_id) {
        log_debug("handle_COMM: own COMM packet => drop");
        return;
    }
    uint8_t neigh_bloom[BLOOM_M];
    memcpy(neigh_bloom, &p->bloom, BLOOM_M);
    bloom_degrade(&neigh_bloom[0], &p->bloom[0]);
    uint8_t own_aggregate_temp[BLOOM_M];
    memcpy(own_aggregate_temp, g_own_id_bloom, BLOOM_M);
    bloom_add(&own_aggregate_temp[0], &g_own_bloom[0]);
    bloom_add(&own_aggregate_temp[0], &neigh_bloom[0]);
    memcpy(g_own_bloom, own_aggregate_temp, BLOOM_M);
    struct Neighbor *neighbor = neighbor_find(p->sender_id);
    if (neighbor) {
        memcpy(&neighbor->bloom, &p->bloom, BLOOM_M);
        memcpy(&neighbor->addr, src, sizeof(Address));
        struct timeval tv;
        gettimeofday(&tv, NULL);
        neighbor->last_updated = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        log_debug("handle_COMM: Updated neighbor 0x%08x from %s", p->sender_id, str_addr(src));
    } else {
        neighbor_add(p->sender_id, p->bloom, src);
    }
}

static void forward_DATA(DATA *p, size_t length) {
    if (!p) {
        log_warning("NULL DATA");
        return;
    }
    if (p->hop_count >= 200) {
        log_warning("max hop count");
        return;
    }
    uint8_t dst_bloom[BLOOM_M];
    bloom_init(dst_bloom, p->dst_id);
    const uint32_t p_own = bloom_probability(g_own_id_bloom, dst_bloom);
    unsigned send_counter = 0;
    log_debug("forward_DATA: check for dst 0x%08x, p_own: %u hop_count: %u", p->dst_id, (unsigned) p_own, p->hop_count);
    struct Neighbor *tmp;
    struct Neighbor *cur;
    HASH_ITER(hh, g_entries, cur, tmp) {
        if (!cur) continue;
        const uint32_t p_neighbor = bloom_probability(&cur->bloom[0], &dst_bloom[0]);
        log_debug("  Neighbor 0x%08x (%s)", cur->sender_id, str_addr(&cur->addr));
        if (p_neighbor > p_own) {
            p->sender_id = gstate.own_id;
            p->hop_count += 1;
            log_debug("    Forwarding DATA packet to neighbor 0x%08x (%s)", cur->sender_id, str_addr(&cur->addr));
            send_ucast_l2(&cur->addr, p, length);
            send_counter++;
        }
    }
    if (send_counter == 0) {
        log_warning("no neighbor");
    } else {
        log_debug("data packet to %u neighbors for dst 0x%08x", send_counter, p->dst_id);
    }
}

static void handle_DATA(const Address *rcv, const Address *src, const Address *dst_from_packet, DATA *p, size_t recv_len) {
    (void)rcv;
    (void)dst_from_packet;
    
    if (recv_len < (ssize_t)sizeof(DATA) || recv_len != (ssize_t)get_data_size(p)) {
        return;
    }
    if (!p || !src) {
        log_warning("NULL packet");
        return;
    }
    if (p->sender_id == gstate.own_id) {
        log_debug("received own packet");
        return;
    }
    log_debug("handle_DATA: got DATA packet from %s (sender_id in packet: 0x%08x) for dst_id 0x%08x, hop_count: %u",
              str_addr(src), p->sender_id, p->dst_id, p->hop_count);

    if (p->dst_id == gstate.own_id) {
        log_debug("handle_DATA: packet arrived at destination (this node 0x%08x)", gstate.own_id);
        const uint8_t* payload = (const uint8_t*)p + sizeof(DATA);
        printf("Received DATA for me (0x%08x) from 0x%08x (last hop %s): '%.*s'\n",
               gstate.own_id, p->sender_id, str_addr(src), (int)p->length, (const char*)payload);
    } else {
        log_debug("handle_DATA: packet is for dst_id 0x%08x, forwarding...", p->dst_id);
        forward_DATA(p, recv_len);
    }
}

static void ext_handler_l2(const Address *rcv, const Address *src, const Address *dst, uint8_t *packet, size_t packet_length) {
    if (packet_length == 0) {
        log_debug("empty packet");
        return;
    }
    if (!packet || !src) {
        log_warning("Received NULL");
        return;
    }
    switch (packet[0]) {
    case TYPE_COMM:
        handle_COMM(rcv, src, dst, (COMM*) packet, packet_length);
        break;
    case TYPE_DATA:
        handle_DATA(rcv, src, dst, (DATA*) packet, packet_length);
        break;
    default:
        log_warning("unknown packet");
    }
}

void send_COMMs(void) {
    static uint64_t g_last_send = 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    gstate.time_now = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    if (g_last_send != 0 && (g_last_send + (uint64_t)COMM_SEND_INTERVAL_SEC * 1000ULL) > gstate.time_now) {
        return;
    } else {
        g_last_send = gstate.time_now;
    }
    COMM data = {
        .type = TYPE_COMM,
        .sender_id = gstate.own_id,
    };
    memcpy(&data.bloom[0], &g_own_bloom[0], sizeof(data.bloom));
    send_COMMs_to_neighbors(&data, sizeof(data));
}

void periodic_handler(int _events, int _fd) {
    (void)_events;
    (void)_fd;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    gstate.time_now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    neighbor_timeout();
    send_COMMs();
}

void receive_packet_and_handle(const uint8_t* packet, size_t length, const Address* src_addr) {
    if (!packet || !src_addr) {
        return;
    }
    Address rcv_addr;
    memset(&rcv_addr, 0, sizeof(rcv_addr));
    rcv_addr.sin_family = AF_INET;
    rcv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    rcv_addr.sin_port = htons(gstate.listen_port);
    Address mock_dst_addr;
    memset(&mock_dst_addr, 0, sizeof(mock_dst_addr));
    mock_dst_addr.sin_family = AF_INET;
    mock_dst_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    mock_dst_addr.sin_port = htons(gstate.listen_port);
    ext_handler_l2(&rcv_addr, src_addr, &mock_dst_addr, (uint8_t*)packet, length);
}

void send_data_packet(uint32_t dst_id, const uint8_t* payload, size_t payload_length) {
    if (payload_length > (2048 - sizeof(DATA))) {
        return;
    }
    size_t total_len = sizeof(DATA) + payload_length;
    uint8_t* packet_buffer = (uint8_t*)malloc(total_len);
    if (!packet_buffer) {
        perror("malloc for packet failed");
        return;
    }
    DATA* data_packet = (DATA*)packet_buffer;
    data_packet->type = TYPE_DATA;
    data_packet->hop_count = 0;
    data_packet->dst_id = dst_id;
    data_packet->sender_id = gstate.own_id;
    data_packet->length = payload_length;
    memcpy(packet_buffer + sizeof(DATA), payload, payload_length);

    uint8_t dst_bloom[BLOOM_M];
    bloom_init(dst_bloom, dst_id);
    uint32_t max_prob = 0;
    struct Neighbor *best_neighbor = NULL;
    struct Neighbor *tmp;
    struct Neighbor *cur;
    HASH_ITER(hh, g_entries, cur, tmp) {
        const uint32_t p_neighbor = bloom_probability(&cur->bloom[0], &dst_bloom[0]);
        log_debug("  Neighbor 0x%08x (%s), p_neighbor: %u", cur->sender_id, str_addr(&cur->addr), (unsigned) p_neighbor);
        if (p_neighbor > max_prob) {
            max_prob = p_neighbor;
            best_neighbor = cur;
        }
    }
    if (best_neighbor) {
        log_debug("Sending DATA to best neighbor 0x%08x (%s)", best_neighbor->sender_id, str_addr(&best_neighbor->addr));
        send_ucast_l2(&best_neighbor->addr, packet_buffer, total_len);
    } else {
        log_warning("No neighbor found to send DATA packet dropped.");
    }
    free(packet_buffer);
}

void send_COMMs_to_neighbors(const void *data, size_t length) {
    if (HASH_COUNT(g_entries) == 0) {
        unsigned send_counter = 0;
        log_debug("No neighbors. Sending COMM packet to contacts...");
        for (int i = 0; i < gstate.num_initial_contacts; ++i) {
            log_debug("Sending COMM packet to %s, size %zu", str_addr(&gstate.initial_contacts[i]), length);
            send_ucast_l2(&gstate.initial_contacts[i], data, length);
            send_counter++;
        }
        if (send_counter == 0) {
            log_debug("nocontacts");
        } else {
            log_debug("Sent COMM packet to %u contacts.", send_counter);
        }
    } else {
        struct Neighbor *tmp;
        struct Neighbor *cur;
        unsigned send_counter = 0;
        HASH_ITER(hh, g_entries, cur, tmp) {
            log_debug("Sending COMM packet to  0x%08x", cur->sender_id);
            send_ucast_l2(&cur->addr, data, length);
            send_counter++;
        }
        log_debug("COMM packet to %u neighbors", send_counter);
    }
}

int initialize_node(void) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    log_debug("node ID 0x%08x on port %d", gstate.own_id, gstate.listen_port);

    bloom_init(g_own_id_bloom, gstate.own_id);
    memset(g_own_bloom, 0, BLOOM_M);

    gstate.udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (gstate.udp_socket < 0) {
        perror("socket creation failed");
        return 1;
    }

    Address server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(gstate.listen_port);

    if (bind(gstate.udp_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(gstate.udp_socket);
        return 1;
    }
    return 0;
}