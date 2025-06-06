// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <time.h>

#define PORT 8080
#define MAX_EVENTS 10
#define BUF_SIZE 1024

void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

void log_with_timestamp(const char *msg) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    printf("[%s] %s\n", buf, msg);
}

void handle_calculation(const char *input, char *response, size_t resp_size) {
    int choice;
    double a, b, result;
    if (sscanf(input, "%d %lf %lf", &choice, &a, &b) != 3) {
        snprintf(response, resp_size, "Invalid input format.");
        return;
    }

    switch (choice) {
        case 1:
            result = a + b;
            snprintf(response, resp_size, "Result: %.2lf + %.2lf = %.2lf", a, b, result);
            break;
        case 2:
            result = a - b;
            snprintf(response, resp_size, "Result: %.2lf - %.2lf = %.2lf", a, b, result);
            break;
        case 3:
            result = a * b;
            snprintf(response, resp_size, "Result: %.2lf * %.2lf = %.2lf", a, b, result);
            break;
        case 4:
            if (b == 0) {
                snprintf(response, resp_size, "Error: Division by zero.");
            } else {
                result = a / b;
                snprintf(response, resp_size, "Result: %.2lf / %.2lf = %.2lf", a, b, result);
            }
            break;
        case 5:
            snprintf(response, resp_size, "Goodbye.");
            break;
        default:
            snprintf(response, resp_size, "Invalid operation choice.");
    }
}

int main() {
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event event, events[MAX_EVENTS];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(server_fd);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    listen(server_fd, SOMAXCONN);
    log_with_timestamp("TCP server listening...");

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    event.data.fd = server_fd;
    event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                // Accept new client
                client_fd = accept(server_fd, NULL, NULL);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }
                set_nonblocking(client_fd);
                event.data.fd = client_fd;
                event.events = EPOLLIN;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
                log_with_timestamp("New client connected.");
            } else {
                char buf[BUF_SIZE], response[BUF_SIZE];
                int client = events[i].data.fd;
                int bytes = recv(client, buf, BUF_SIZE - 1, 0);

                if (bytes <= 0) {
                    close(client);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client, NULL);
                    log_with_timestamp("Client disconnected.");
                } else {
                    buf[bytes] = '\0';
                    log_with_timestamp("Received client message.");
                    handle_calculation(buf, response, BUF_SIZE);
                    send(client, response, strlen(response), 0);

                    // Close client if choice 5 (exit)
                    if (strncmp(buf, "5", 1) == 0) {
                        close(client);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client, NULL);
                        log_with_timestamp("Client requested exit.");
                    }
                }
            }
        }
    }

    close(server_fd);
    return 0;
}
