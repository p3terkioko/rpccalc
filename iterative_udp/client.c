// udp_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUF_SIZE];
    int choice;
    double a, b;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; 
    server_addr.sin_port = htons(PORT); 
    server_addr.sin_addr.s_addr = INADDR_ANY;

    while (1) {
        printf("\n===== SIMPLE CALCULATOR CLIENT =====\n");
        printf("1. Add\n2. Subtract\n3. Multiply\n4. Divide\n5. Exit\n");
        printf("Choose operation: ");
        scanf("%d", &choice);

        if (choice == 5) {
            sprintf(buffer, "%d 0 0", choice);
            sendto(sockfd, buffer, strlen(buffer), 0, (const struct sockaddr *)&server_addr, addr_len);
            printf("Exiting...\n");
            break;
        }

        printf("Enter two numbers: ");
        scanf("%lf %lf", &a, &b);

        // Send operation to server
        snprintf(buffer, BUF_SIZE, "%d %lf %lf", choice, a, b);
        sendto(sockfd, buffer, strlen(buffer), 0, (const struct sockaddr *)&server_addr, addr_len);

        // Receive response
        recvfrom(sockfd, buffer, BUF_SIZE, 0, (struct sockaddr *)&server_addr, &addr_len);
        buffer[strcspn(buffer, "\n")] = '\0';
        printf("%s\n", buffer);
    }

    close(sockfd);
    return 0;
}
