// udp_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUF_SIZE];

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; 
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(PORT);

    // Bind the socket
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(1);
    }

    printf("UDP Calculator Server is running on port %d...\n", PORT);

    while (1) {
        // Receive message
        recvfrom(sockfd, buffer, BUF_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);

        int choice;
        double a, b, result;
        sscanf(buffer, "%d %lf %lf", &choice, &a, &b);

        // Handle operation
        if (choice == 5) {
            printf("Client requested exit.\n");
            continue;
        }

        switch (choice) {
            case 1: result = a + b; break;
            case 2: result = a - b; break;
            case 3: result = a * b; break;
            case 4: 
                if (b == 0) {
                    snprintf(buffer, BUF_SIZE, "Error: Division by zero");
                    sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
                    continue;
                }
                result = a / b; 
                break;
            default:
                snprintf(buffer, BUF_SIZE, "Invalid choice");
                sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
                continue;
        }

        // Send result
        snprintf(buffer, BUF_SIZE, "Result: %.2lf", result);
        sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
    }

    close(sockfd);
    return 0;
}
