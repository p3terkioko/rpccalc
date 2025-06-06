// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Added for inet_pton
#include <fcntl.h>
#include <time.h>
#include <getopt.h> // Added for getopt_long

// #define PORT 8080 // Will be set by command line argument
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

int main(int argc, char *argv[]) { // Added argc and argv
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event event, events[MAX_EVENTS];

    char *gateway_host = NULL;
    int gateway_port = -1;
    char *my_host = NULL;
    int my_port = -1;
    char *server_name = NULL;

    // Parse command line arguments
    struct option long_options[] = {
        {"gateway-host", required_argument, 0, 'g'},
        {"gateway-port", required_argument, 0, 'p'},
        {"my-host", required_argument, 0, 'h'},
        {"my-port", required_argument, 0, 'm'},
        {"server-name", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "g:p:h:m:s:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'g':
                gateway_host = optarg;
                break;
            case 'p':
                gateway_port = atoi(optarg);
                break;
            case 'h':
                my_host = optarg;
                break;
            case 'm':
                my_port = atoi(optarg);
                break;
            case 's':
                server_name = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s --gateway-host <host> --gateway-port <port> --my-host <host> --my-port <port> --server-name <name>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (!gateway_host || gateway_port == -1 || !my_host || my_port == -1 || !server_name) {
        fprintf(stderr, "Missing required arguments.\n");
        fprintf(stderr, "Usage: %s --gateway-host <host> --gateway-port <port> --my-host <host> --my-port <port> --server-name <name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    log_with_timestamp("Server starting with provided arguments.");


    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(server_fd);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(my_port); // Use my_port from command line
    // addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces initially
                                        // For registration, my_host is used.
    if (inet_pton(AF_INET, my_host, &addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        // We might want to allow INADDR_ANY for listening, but use specific my_host for registration.
        // For now, let's assume my_host is the listen IP.
        log_with_timestamp("Using INADDR_ANY for listening due to my_host issue for bind.");
        addr.sin_addr.s_addr = INADDR_ANY; // Fallback or specific configuration needed
    }


    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        // If specific my_host bind fails, try INADDR_ANY as a fallback for listening
        log_with_timestamp("Bind to specific my_host failed, trying INADDR_ANY.");
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("Bind failed on INADDR_ANY as well");
            exit(EXIT_FAILURE);
        }
    }

    listen(server_fd, SOMAXCONN);
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "TCP server listening on %s:%d...", my_host, my_port);
    log_with_timestamp(log_msg);

    // Register with gateway
    int reg_sock;
    struct sockaddr_in gateway_addr;
    char reg_msg[512];

    snprintf(reg_msg, sizeof(reg_msg), "type=TCP;host=%s;port=%d;name=%s;ops=add,subtract,multiply,divide",
             my_host, my_port, server_name);

    if ((reg_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation for registration failed");
        // Continue without registration? Or exit? For now, log and continue.
    } else {
        memset(&gateway_addr, 0, sizeof(gateway_addr));
        gateway_addr.sin_family = AF_INET;
        gateway_addr.sin_port = htons(gateway_port);

        if (inet_pton(AF_INET, gateway_host, &gateway_addr.sin_addr) <= 0) {
            perror("Invalid gateway address for registration");
            close(reg_sock); // Close the created socket
        } else {
            log_with_timestamp("Attempting to register with gateway...");
            if (sendto(reg_sock, reg_msg, strlen(reg_msg), 0, (const struct sockaddr *)&gateway_addr, sizeof(gateway_addr)) < 0) {
                perror("sendto registration message failed");
                char error_log[512];
                snprintf(error_log, sizeof(error_log), "Failed to send registration to %s:%d. Error: %s", gateway_host, gateway_port, strerror(errno));
                log_with_timestamp(error_log);
            } else {
                snprintf(log_msg, sizeof(log_msg), "Registration message sent to %s:%d: %s", gateway_host, gateway_port, reg_msg);
                log_with_timestamp(log_msg);
            }
            close(reg_sock);
        }
    }

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
