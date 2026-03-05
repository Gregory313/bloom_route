#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "conf.h"
#include "bloom.h"
#include "neighbors.h"
#include "control.h"
#include "utils.h"

static void handle_user_command(const char *cmd) {
    // Пропускаем начальные пробелы
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;
    
    char command[256];
    strncpy(command, cmd, sizeof(command) - 1);
    command[sizeof(command) - 1] = '\0';
    
    // Обрезаем пробелы в конце (уже сделано, но на всякий случай)
    size_t len = strlen(command);
    while (len > 0 && (command[len-1] == ' ' || command[len-1] == '\t')) {
        command[len-1] = '\0';
        len--;
    }
    
    if (strncmp(command, "send ", 5) == 0) {
        uint32_t dst_id;
        char *msg = command + 5;
        // Пропускаем пробелы после send
        while (*msg == ' ' || *msg == '\t') msg++;
        char *space = strchr(msg, ' ');
        if (!space) {
            printf("Usage: send <dst_id> <message>\n");
            return;
        }
        *space = '\0';
        dst_id = strtoul(msg, NULL, 16);
        msg = space + 1;
        while (*msg == ' ' || *msg == '\t') msg++;
        if (*msg == '\0') {
            printf("Message cannot be empty\n");
            return;
        }
        send_data_packet(dst_id, (const uint8_t*)msg, strlen(msg));
    }
    else if (strcmp(command, "neighbors") == 0) {
        struct Neighbor *cur, *tmp;
        if (HASH_COUNT(g_entries) == 0) {
            printf("No neighbors.\n");
        } else {
            printf("Neighbors:\n");
            HASH_ITER(hh, g_entries, cur, tmp) {
                printf("  0x%08x (%s) last updated %llu\n",
                       cur->sender_id, str_addr(&cur->addr),
                       (unsigned long long)cur->last_updated);
            }
        }
    }
    else if (strcmp(command, "exit") == 0) {
    }
    else {
        printf("Unknown command. Available: send <hex_dst> <msg>, neighbors, exit\n");
    }
}


static int run_event_loop(void) {
    uint8_t recv_buffer[2048];
    Address sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    printf("Node 0x%08x listening on port %d. Own ID Bloom: ", gstate.own_id, gstate.listen_port);
    char own_id_bloom_str[BLOOM_M * 4 + 1];
    str_bloom(own_id_bloom_str, g_own_id_bloom);
    printf("[%s]\n", own_id_bloom_str);

    while (1) {
        periodic_handler(0, 0);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(gstate.udp_socket, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = (gstate.udp_socket > STDIN_FILENO) ? gstate.udp_socket : STDIN_FILENO;
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0 && errno != EINTR) {
            perror("select error");
            return 1;
        }

        if (activity > 0) {
            if (FD_ISSET(gstate.udp_socket, &read_fds)) {
                addr_len = sizeof(sender_addr);
                ssize_t recv_len = recvfrom(gstate.udp_socket, recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr *)&sender_addr, &addr_len);
                if (recv_len > 0) {
                    receive_packet_and_handle(recv_buffer, recv_len, &sender_addr);
                } else if (recv_len < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                    perror("recvfrom error");
                    return 1;
                }
            }

            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                char command_buffer[256];
                if (fgets(command_buffer, sizeof(command_buffer), stdin) != NULL) {
                    // Удаляем символы новой строки и возврата каретки
                    size_t len = strlen(command_buffer);
                    while (len > 0 && (command_buffer[len-1] == '\n' || command_buffer[len-1] == '\r')) {
                        command_buffer[len-1] = '\0';
                        len--;
                    }
                    
                    // Пропускаем пустые строки
                    if (len == 0) continue;
                    
                    // Обрезаем пробелы в начале и конце
                    char *start = command_buffer;
                    while (*start == ' ' || *start == '\t') start++;
                    char *end = command_buffer + len - 1;
                    while (end > start && (*end == ' ' || *end == '\t')) {
                        *end = '\0';
                        end--;
                    }
                    
                    if (strcmp(start, "exit") == 0) {
                        log_debug("Exiting...");
                        break;
                    }
                    handle_user_command(start);
                } else if (feof(stdin)) {
                    log_debug("EOF on stdin. Exiting...");
                    break;
                }
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    // Инициализация gstate
    memset(&gstate, 0, sizeof(gstate));
    
    // Парсинг аргументов командной строки
    int opt;
    gstate.num_initial_contacts = 0;
    
    while ((opt = getopt(argc, argv, "p:i:c:")) != -1) {
        switch (opt) {
            case 'p':
                gstate.listen_port = atoi(optarg);
                break;
            case 'i':
                gstate.own_id = strtoul(optarg, NULL, 16);
                break;
            case 'c': {
                char ip[64];
                int port;
                if (sscanf(optarg, "%63[^:]:%d", ip, &port) == 2) {
                    gstate.initial_contacts[gstate.num_initial_contacts].sin_family = AF_INET;
                    gstate.initial_contacts[gstate.num_initial_contacts].sin_addr.s_addr = inet_addr(ip);
                    gstate.initial_contacts[gstate.num_initial_contacts].sin_port = htons(port);
                    gstate.num_initial_contacts++;
                }
                break;
            }
            default:
                fprintf(stderr, "Usage: %s -p <port> -i <hex_id> [-c <ip:port> ...]\n", argv[0]);
                return 1;
        }
    }
    
    if (gstate.listen_port == 0 || gstate.own_id == 0) {
        fprintf(stderr, "Missing required arguments\n");
        fprintf(stderr, "Usage: %s -p <port> -i <hex_id> [-c <ip:port> ...]\n", argv[0]);
        return 1;
    }
    
    if (initialize_node() != 0) {
        return 1;
    }
    
    return run_event_loop();
}