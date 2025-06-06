// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];
    int choice;
    double a, b;

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    printf("Connected to calculator server.\n");

    while (1) {
        printf("\n===== SIMPLE CALCULATOR CLIENT =====\n");
        printf("1. Add\n2. Subtract\n3. Multiply\n4. Divide\n5. Exit\n");
        printf("Choose operation: ");
        scanf("%d", &choice);

        if (choice == 5) {
            snprintf(buffer, BUF_SIZE, "%d 0 0", choice);
            send(sock, buffer, strlen(buffer), 0);
            printf("Exiting...\n");
            break;
        }

        printf("Enter two numbers: ");
        scanf("%lf %lf", &a, &b);

        snprintf(buffer, BUF_SIZE, "%d %lf %lf", choice, a, b);
        send(sock, buffer, strlen(buffer), 0);

        memset(buffer, 0, BUF_SIZE);
        recv(sock, buffer, BUF_SIZE, 0);
        printf("%s\n", buffer);
    }

    close(sock);
    return 0;
}
