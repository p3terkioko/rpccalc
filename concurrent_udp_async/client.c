// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9090
#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

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
        return EXIT_FAILURE;
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sockfd);
        return EXIT_FAILURE;
    }

    while (1) {
        printf("\n===== SIMPLE UDP CALCULATOR CLIENT =====\n");
        printf("1. Add\n2. Subtract\n3. Multiply\n4. Divide\n5. Exit\n");
        printf("Choose operation: ");
        scanf("%d", &choice);

        if (choice == 5) {
            snprintf(buffer, BUF_SIZE, "%d 0 0", choice);
            sendto(sockfd, buffer, strlen(buffer), 0,
                   (struct sockaddr *)&server_addr, addr_len);
            printf("Exit message sent to server.\n");
            break;
        }

        printf("Enter two numbers: ");
        if (scanf("%lf %lf", &a, &b) != 2) {
            printf("Invalid input. Try again.\n");
            while (getchar() != '\n'); // flush input buffer
            continue;
        }

        snprintf(buffer, BUF_SIZE, "%d %lf %lf", choice, a, b);
        sendto(sockfd, buffer, strlen(buffer), 0,
               (struct sockaddr *)&server_addr, addr_len);

        int bytes = recvfrom(sockfd, buffer, BUF_SIZE - 1, 0,
                             (struct sockaddr *)&server_addr, &addr_len);
        if (bytes < 0) {
            perror("recvfrom failed");
            break;
        }

        buffer[bytes] = '\0';
        printf("Server: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
