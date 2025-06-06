// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];
    int choice;
    double a, b;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return 1;
    }

    while (1) {
        printf("\n===== SIMPLE TCP CALCULATOR CLIENT =====\n");
        printf("1. Add\n2. Subtract\n3. Multiply\n4. Divide\n5. Exit\n");
        printf("Choose operation: ");
        scanf("%d", &choice);

        if (choice == 5) {
            snprintf(buffer, BUF_SIZE, "%d 0 0", choice);
            send(sockfd, buffer, strlen(buffer), 0);
            printf("Exiting...\n");
            break;
        }

        printf("Enter two numbers: ");
        scanf("%lf %lf", &a, &b);
        snprintf(buffer, BUF_SIZE, "%d %lf %lf", choice, a, b);
        send(sockfd, buffer, strlen(buffer), 0);

        int bytes = recv(sockfd, buffer, BUF_SIZE - 1, 0);
        if (bytes <= 0) {
            printf("Server closed connection.\n");
            break;
        }

        buffer[bytes] = '\0';
        printf("Server: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
