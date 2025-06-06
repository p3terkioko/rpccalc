// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>

#define PORT 8080
#define BUF_SIZE 1024

int client_count = 0;

void log_to_file(const char *message) {
    FILE *logfile = fopen("server.log", "a");
    if (logfile != NULL) {
        fprintf(logfile, "%s\n", message);
        fclose(logfile);
    }
}

char *get_timestamp() {
    static char buffer[64];
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local);
    return buffer;
}

void handle_client(int client_sock, int session_id, const char *client_ip, int client_port) {
    char buffer[BUF_SIZE];

    char msg[256];
    snprintf(msg, sizeof(msg), "[%s] [Client #%d | PID %d] Connected: %s:%d",
             get_timestamp(), session_id, getpid(), client_ip, client_port);
    printf("%s\n", msg);
    log_to_file(msg);

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes = read(client_sock, buffer, BUF_SIZE);
        if (bytes <= 0) {
            snprintf(msg, sizeof(msg), "[%s] [Client #%d | PID %d] Disconnected.",
                     get_timestamp(), session_id, getpid());
            printf("%s\n", msg);
            log_to_file(msg);
            break;
        }

        int choice;
        double a, b, result;
        sscanf(buffer, "%d %lf %lf", &choice, &a, &b);

        if (choice == 5) break;

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
                    write(client_sock, buffer, strlen(buffer));
                    continue;
            }
            snprintf(buffer, BUF_SIZE, "Result: %.2lf", result);
        }

        write(client_sock, buffer, strlen(buffer));
    }

    close(client_sock);
    exit(0); // End child
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    signal(SIGCHLD, SIG_IGN); // Prevent zombies

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(1);
    }

    printf("Concurrent TCP Calculator Server listening on port %d...\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        char *client_ip = inet_ntoa(address.sin_addr);
        int client_port = ntohs(address.sin_port);

        client_count++;  // Sequential session ID
        int session_id = client_count;

        pid_t pid = fork();

        if (pid == 0) {
            // Child
            close(server_fd);
            handle_client(new_socket, session_id, client_ip, client_port);
        } else if (pid > 0) {
            // Parent
            close(new_socket);
        } else {
            perror("Fork failed");
        }
    }

    close(server_fd);
    return 0;
}
