// aio_udp_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 9090
#define BUF_SIZE 1024
#define MAX_EVENTS 10

void log_message(const char *msg) {
    FILE *logfile = fopen("server.log", "a");
    if (!logfile) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(logfile, "[%s] %s\n", time_str, msg);
    fclose(logfile);
}

int make_socket_non_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int sockfd, epfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    make_socket_non_blocking(sockfd);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(1);
    }

    epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1 failed");
        exit(1);
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    printf("Async UDP server using epoll running on port %d...\n", PORT);
    log_message("Async server started.");

    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == sockfd) {
                char buffer[BUF_SIZE];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                int bytes = recvfrom(sockfd, buffer, BUF_SIZE - 1, 0,
                                     (struct sockaddr *)&client_addr, &addr_len);
                if (bytes <= 0) continue;

                buffer[bytes] = '\0';

                int choice;
                double a, b, result;
                char response[BUF_SIZE];

                sscanf(buffer, "%d %lf %lf", &choice, &a, &b);

                if (choice == 5) {
                    snprintf(response, sizeof(response), "Goodbye!");
                } else {
                    switch (choice) {
                        case 1: result = a + b; break;
                        case 2: result = a - b; break;
                        case 3: result = a * b; break;
                        case 4:
                            if (b == 0) {
                                snprintf(response, sizeof(response), "Division by zero!");
                                goto send;
                            }
                            result = a / b;
                            break;
                        default:
                            snprintf(response, sizeof(response), "Invalid operation");
                            goto send;
                    }
                    snprintf(response, sizeof(response), "Result: %.2lf", result);
                }

            send:
                sendto(sockfd, response, strlen(response), 0,
                       (struct sockaddr *)&client_addr, addr_len);

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
                int port = ntohs(client_addr.sin_port);

                char logbuf[256];
                snprintf(logbuf, sizeof(logbuf),
                         "Request from %s:%d -> \"%s\" -> \"%s\"",
                         ip, port, buffer, response);
                log_message(logbuf);
            }
        }
    }

    close(sockfd);
    return 0;
}
