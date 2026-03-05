#include "conf.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

uint8_t g_own_id_bloom[BLOOM_M];
uint8_t g_own_bloom[BLOOM_M];
struct Neighbor *g_entries = NULL;
GState gstate;

char* str_addr(const Address* addr) {
    static char buf[INET_ADDRSTRLEN + 6];
    if (!addr) return "(null)";
    inet_ntop(AF_INET, &addr->sin_addr, buf, INET_ADDRSTRLEN);
    sprintf(buf + strlen(buf), ":%u", ntohs(addr->sin_port));
    return buf;
}

bool address_is_broadcast(const Address* addr) {
    (void)addr;
    return false;
}

bool address_equal(const Address* addr1, const Address* addr2) {
    if (!addr1 || !addr2) return false;
    return addr1->sin_addr.s_addr == addr2->sin_addr.s_addr &&
           addr1->sin_port == addr2->sin_port;
}

size_t get_data_size(const DATA *data) {
    if (!data) return 0;
    return sizeof(DATA) + data->length;
}

void send_ucast_l2(const Address* dst_addr, const void *data, size_t length) {
    if (dst_addr == NULL || data == NULL || length == 0) {
        log_warning("NULL packet.");
        return;
    }
    ssize_t sent_len = sendto(gstate.udp_socket, data, length, 0, (const struct sockaddr*)dst_addr, sizeof(struct sockaddr_in));
    if (sent_len < 0) {
        if (errno != EPERM) {
            perror("sendto failed");
        } else {
            log_debug("sendto failed");
        }
    } else if ((size_t)sent_len != length) {
        log_warning("sendto sent only %zd of %zu bytes to %s", sent_len, length, str_addr(dst_addr));
    }
}