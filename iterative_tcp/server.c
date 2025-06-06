// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#define PORT 8080
#define BUF_SIZE 1024

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[BUF_SIZE];

    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(1);
    }

    // Setup server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }

    // Listen
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(1);
    }

    printf("TCP Calculator Server listening on port %d...\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Client connected.\n");

        while (1) {
            memset(buffer, 0, BUF_SIZE);
            int bytes = read(new_socket, buffer, BUF_SIZE);
            if (bytes <= 0) {
                printf("Client disconnected.\n");
                break;
            }

            int choice;
            double a, b, result;
            sscanf(buffer, "%d %lf %lf", &choice, &a, &b);

            if (choice == 5) {
                printf("Client requested exit.\n");
                break;
            }

            if (choice == 4 && b == 0) {
                snprintf(buffer, BUF_SIZE, "Error: Division by zero");
            } else {
                switch (choice) {
                    case 1: result = a + b; break;
                    case 2: result = a - b; break;
                    case 3: result = a * b; break;
                    case 4: result = a / b; break;
                    default:
                        snprintf(buffer, BUF_SIZE, "Invalid choice");
                        write(new_socket, buffer, strlen(buffer));
                        continue;
                }
                snprintf(buffer, BUF_SIZE, "Result: %.2lf", result);
            }

            write(new_socket, buffer, strlen(buffer));
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}
