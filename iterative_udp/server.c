// udp_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <getopt.h> // Added for getopt_long
#include <errno.h> // Added for errno
#include <time.h>   // Added for timestamp logging

// #define PORT 8080 // Will be set by command line argument
#define BUF_SIZE 1024

// Function for logging with timestamp (similar to TCP server)
void log_with_timestamp(const char *msg) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    printf("[%s] %s\n", buf, msg);
}


int main(int argc, char *argv[]) { // Added argc and argv
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUF_SIZE];

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

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    // server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(my_port); // Use my_port from command line

    if (inet_pton(AF_INET, my_host, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported for server listening");
        log_with_timestamp("Using INADDR_ANY for listening due to my_host issue for bind.");
        server_addr.sin_addr.s_addr = INADDR_ANY; // Fallback
    }


    // Bind the socket
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        // If specific my_host bind fails, try INADDR_ANY as a fallback
        log_with_timestamp("Bind to specific my_host failed, trying INADDR_ANY.");
        server_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Bind failed on INADDR_ANY as well");
            close(sockfd);
            exit(1);
        }
    }

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "UDP server listening on %s:%d...", my_host, my_port);
    log_with_timestamp(log_msg);

    // Register with gateway using a new temporary UDP socket
    int reg_sock;
    struct sockaddr_in gateway_addr_reg; // Use a different name to avoid conflict
    char reg_msg[512];

    snprintf(reg_msg, sizeof(reg_msg), "type=UDP;host=%s;port=%d;name=%s;ops=add,subtract,multiply,divide",
             my_host, my_port, server_name);

    if ((reg_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Temporary UDP socket creation for registration failed");
        // Log and continue without registration
        log_with_timestamp("Proceeding without gateway registration due to socket creation failure.");
    } else {
        memset(&gateway_addr_reg, 0, sizeof(gateway_addr_reg));
        gateway_addr_reg.sin_family = AF_INET;
        gateway_addr_reg.sin_port = htons(gateway_port);

        if (inet_pton(AF_INET, gateway_host, &gateway_addr_reg.sin_addr) <= 0) {
            perror("Invalid gateway address for registration");
            log_with_timestamp("Proceeding without gateway registration due to invalid gateway address.");
            close(reg_sock);
        } else {
            log_with_timestamp("Attempting to register with gateway...");
            if (sendto(reg_sock, reg_msg, strlen(reg_msg), 0, (const struct sockaddr *)&gateway_addr_reg, sizeof(gateway_addr_reg)) < 0) {
                perror("sendto registration message failed");
                char error_log[512];
                snprintf(error_log, sizeof(error_log), "Failed to send registration to %s:%d. Error: %s", gateway_host, gateway_port, strerror(errno));
                log_with_timestamp(error_log);
            } else {
                char success_log[512];
                snprintf(success_log, sizeof(success_log), "Registration message sent to %s:%d: %s", gateway_host, gateway_port, reg_msg);
                log_with_timestamp(success_log);
            }
            close(reg_sock); // Close the temporary socket
        }
    }


    log_with_timestamp("UDP Calculator Server is now fully running.");

    while (1) {
        // Receive message
        memset(buffer, 0, BUF_SIZE); // Clear buffer before receiving
        int n = recvfrom(sockfd, buffer, BUF_SIZE -1 , 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }
        buffer[n] = '\0'; // Null-terminate the received data

        // Log client address and message
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        char recv_log[BUF_SIZE + 100];
        snprintf(recv_log, sizeof(recv_log), "Received from %s:%d - %s", client_ip, ntohs(client_addr.sin_port), buffer);
        log_with_timestamp(recv_log);


        int choice;
        double a, b, result;
        // sscanf(buffer, "%d %lf %lf", &choice, &a, &b); // Original sscanf
        if (sscanf(buffer, "%d %lf %lf", &choice, &a, &b) != 3) {
            char err_resp[100];
            snprintf(err_resp, sizeof(err_resp), "Invalid input format. Expected: <choice> <num1> <num2>");
            log_with_timestamp("Sending error response for invalid input format.");
            sendto(sockfd, err_resp, strlen(err_resp), 0, (struct sockaddr *)&client_addr, addr_len);
            continue;
        }


        // Handle operation
        if (choice == 5) {
            log_with_timestamp("Client requested exit.");
            char exit_resp[] = "Goodbye.";
            sendto(sockfd, exit_resp, strlen(exit_resp), 0, (struct sockaddr *)&client_addr, addr_len);
            continue;
        }

        char calc_log[100];
        snprintf(calc_log, sizeof(calc_log), "Performing operation %d for %lf and %lf", choice, a,b);
        log_with_timestamp(calc_log);

        switch (choice) {
            case 1: result = a + b; break;
            case 2: result = a - b; break;
            case 3: result = a * b; break;
            case 4:
                if (b == 0) {
                    log_with_timestamp("Error: Division by zero.");
                    snprintf(buffer, BUF_SIZE, "Error: Division by zero");
                    sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
                    continue;
                }
                result = a / b;
                break;
            default:
                log_with_timestamp("Invalid operation choice received.");
                snprintf(buffer, BUF_SIZE, "Invalid operation choice.");
                sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
                continue;
        }

        // Send result
        snprintf(buffer, BUF_SIZE, "Result: %.2lf", result);
        log_with_timestamp("Sending result to client.");
        sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
    }

    log_with_timestamp("UDP Server shutting down.");
    close(sockfd);
    return 0;
}
