// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9090
#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(1);
    }

    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUF_SIZE];
    int choice;
    double a, b;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    while (1) {
        printf("\n===== SIMPLE CALCULATOR CLIENT =====\n");
        printf("1. Add\n2. Subtract\n3. Multiply\n4. Divide\n5. Exit\n");
        printf("Choose operation: ");
        scanf("%d", &choice);

        if (choice == 5) {
            snprintf(buffer, BUF_SIZE, "%d 0 0", choice);
            sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&server_addr, addr_len);
            recvfrom(sockfd, buffer, BUF_SIZE, 0, NULL, NULL);
            buffer[strcspn(buffer, "\n")] = '\0';
            printf("%s\n", buffer);
            break;
        }

        printf("Enter two numbers: ");
        scanf("%lf %lf", &a, &b);

        snprintf(buffer, BUF_SIZE, "%d %lf %lf", choice, a, b);
        sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&server_addr, addr_len);
        recvfrom(sockfd, buffer, BUF_SIZE, 0, NULL, NULL);
        buffer[strcspn(buffer, "\n")] = '\0';
        printf("%s\n", buffer);
    }

    close(sockfd);
    return 0;
}
