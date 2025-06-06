// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PORT 9090
#define BUF_SIZE 1024
#define LOG_FILE "server.log"

int session_counter = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Get current timestamp string
void current_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", tm_info);
}

// Thread-safe logger
void log_message(const char *msg) {
    FILE *log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) return;

    char timestamp[64];
    current_timestamp(timestamp, sizeof(timestamp));

    fprintf(log_fp, "%s %s\n", timestamp, msg);
    fclose(log_fp);
}

typedef struct {
    int client_sock;
    int client_id;
    struct sockaddr_in client_addr;
} ClientData;

void *handle_client(void *arg) {
    ClientData *data = (ClientData *)arg;
    char buffer[BUF_SIZE];
    int choice;
    double a, b, result;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(data->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(data->client_addr.sin_port);

    time_t start_time = time(NULL);

    printf("Client #%d connected [%s:%d]\n", data->client_id, client_ip, client_port);

    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf),
             "Client #%d connected from %s:%d", data->client_id, client_ip, client_port);
    log_message(log_buf);

    while (1) {
        int bytes_received = recv(data->client_sock, buffer, BUF_SIZE, 0);
        if (bytes_received <= 0) {
            snprintf(log_buf, sizeof(log_buf),
                     "Client #%d disconnected unexpectedly", data->client_id);
            log_message(log_buf);
            break;
        }

        buffer[bytes_received] = '\0';
        sscanf(buffer, "%d %lf %lf", &choice, &a, &b);

        if (choice == 5) {
            snprintf(buffer, BUF_SIZE, "Client #%d disconnected gracefully.\n", data->client_id);
            send(data->client_sock, buffer, strlen(buffer), 0);
            snprintf(log_buf, sizeof(log_buf),
                     "Client #%d exited gracefully", data->client_id);
            log_message(log_buf);
            break;
        }

        switch (choice) {
            case 1: result = a + b; break;
            case 2: result = a - b; break;
            case 3: result = a * b; break;
            case 4:
                if (b == 0) {
                    snprintf(buffer, BUF_SIZE, "Error: Division by zero.");
                    send(data->client_sock, buffer, strlen(buffer), 0);
                    continue;
                } else {
                    result = a / b;
                }
                break;
            default:
                snprintf(buffer, BUF_SIZE, "Invalid operation.");
                send(data->client_sock, buffer, strlen(buffer), 0);
                continue;
        }

        snprintf(buffer, BUF_SIZE, "Result: %.2lf", result);
        send(data->client_sock, buffer, strlen(buffer), 0);
    }

    close(data->client_sock);
    time_t end_time = time(NULL);
    double duration = difftime(end_time, start_time);

    printf("Client #%d session ended. Duration: %.0f seconds.\n", data->client_id, duration);

    snprintf(log_buf, sizeof(log_buf),
             "Client #%d session ended. Duration: %.0f seconds.", data->client_id, duration);
    log_message(log_buf);

    free(data);
    pthread_exit(NULL);
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(1);
    }

    listen(server_sock, 10);
    printf("TCP Server listening on port %d...\n", PORT);
    log_message("Server started and listening...");

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) continue;

        ClientData *data = malloc(sizeof(ClientData));
        data->client_sock = client_sock;
        data->client_addr = client_addr;

        pthread_mutex_lock(&lock);
        data->client_id = ++session_counter;
        pthread_mutex_unlock(&lock);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, data);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}
