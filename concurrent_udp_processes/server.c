// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define PORT 9090
#define BUF_SIZE 1024

void handle_client(struct sockaddr_in client_addr, char *initial_msg);

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUF_SIZE];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    printf("UDP Server listening on port %d...\n", PORT);

    while (1) {
        int n = recvfrom(sockfd, buffer, BUF_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) continue;

        buffer[n] = '\0';

        pid_t pid = fork();
        if (pid == 0) { // Child
            close(sockfd);
            handle_client(client_addr, buffer);
            exit(0);
        }
        // Parent continues listening
    }

    return 0;
}

void handle_client(struct sockaddr_in client_addr, char *initial_msg) {
    char buffer[BUF_SIZE];
    int sockfd;
    socklen_t addr_len = sizeof(client_addr);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Child socket creation failed");
        exit(1);
    }

    time_t start_time = time(NULL);
    static int session_id = 0;
    int client_id = ++session_id;

    printf("Client #%d connected [%s:%d]\n", client_id,
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    while (1) {
        int choice;
        double a, b, result;
        sscanf(initial_msg, "%d %lf %lf", &choice, &a, &b);

        if (choice == 5) {
            snprintf(buffer, BUF_SIZE, "Client #%d disconnected gracefully.", client_id);
            sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
            break;
        }

        switch (choice) {
            case 1: result = a + b; break;
            case 2: result = a - b; break;
            case 3: result = a * b; break;
            case 4:
                if (b == 0) {
                    snprintf(buffer, BUF_SIZE, "Error: Division by zero.");
                    sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
                    goto receive_next;
                } else {
                    result = a / b;
                }
                break;
            default:
                snprintf(buffer, BUF_SIZE, "Invalid operation.");
                sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);
                goto receive_next;
        }

        snprintf(buffer, BUF_SIZE, "Client #%d result: %.2lf", client_id, result);
        sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, addr_len);

    receive_next:
        int n = recvfrom(sockfd, buffer, BUF_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n <= 0) break;
        buffer[n] = '\0';
        strcpy(initial_msg, buffer);  // reuse same logic
    }

    time_t end_time = time(NULL);
    double duration = difftime(end_time, start_time);
    printf("Client #%d session ended. Duration: %.0f seconds.\n", client_id, duration);
    close(sockfd);
}
