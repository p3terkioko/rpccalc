// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 9090
#define BUF_SIZE 1024

typedef struct {
    struct sockaddr_in client_addr;
    char buffer[BUF_SIZE];
    int client_id;
    time_t start_time;
    int sockfd;  // Socket descriptor passed to thread
} ClientRequest;

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
int client_counter = 0;

void log_message(const char *msg) {
    pthread_mutex_lock(&log_mutex);

    FILE *logfile = fopen("server.log", "a");
    if (logfile == NULL) {
        perror("Failed to open log file");
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char time_str[26];
    strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(logfile, "[%s] %s\n", time_str, msg);
    fclose(logfile);

    pthread_mutex_unlock(&log_mutex);
}

void *handle_request(void *arg) {
    ClientRequest *req = (ClientRequest *)arg;

    int choice;
    double a, b, result;
    char response[BUF_SIZE];

    sscanf(req->buffer, "%d %lf %lf", &choice, &a, &b);

    if (choice == 5) {
        time_t end_time = time(NULL);
        double duration = difftime(end_time, req->start_time);

        snprintf(response, sizeof(response),
                 "Client #%d exited. Session duration: %.2f seconds.",
                 req->client_id, duration);

        sendto(req->sockfd, response, strlen(response), 0,
               (struct sockaddr *)&req->client_addr, sizeof(req->client_addr));

        char logbuf[256];
        snprintf(logbuf, sizeof(logbuf),
                 "Client #%d exited gracefully. Duration: %.2f seconds.",
                 req->client_id, duration);
        log_message(logbuf);

        free(req);
        pthread_exit(NULL);
    }

    switch (choice) {
        case 1:
            result = a + b;
            snprintf(response, sizeof(response), "Result: %.2lf", result);
            break;
        case 2:
            result = a - b;
            snprintf(response, sizeof(response), "Result: %.2lf", result);
            break;
        case 3:
            result = a * b;
            snprintf(response, sizeof(response), "Result: %.2lf", result);
            break;
        case 4:
            if (b == 0) {
                snprintf(response, sizeof(response), "Error: Division by zero!");
            } else {
                result = a / b;
                snprintf(response, sizeof(response), "Result: %.2lf", result);
            }
            break;
        default:
            snprintf(response, sizeof(response), "Invalid choice.");
    }

    sendto(req->sockfd, response, strlen(response), 0,
           (struct sockaddr *)&req->client_addr, sizeof(req->client_addr));

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &req->client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(req->client_addr.sin_port);

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf),
             "Request from Client #%d (%s:%d): \"%s\" -> %s",
             req->client_id, client_ip, client_port, req->buffer, response);
    log_message(logbuf);

    free(req);
    pthread_exit(NULL);
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("UDP Concurrent Server running on port %d...\n", PORT);
    log_message("Server started.");

    while (1) {
        ClientRequest *req = malloc(sizeof(ClientRequest));
        if (!req) {
            perror("Failed to allocate memory");
            continue;
        }

        int n = recvfrom(sockfd, req->buffer, BUF_SIZE - 1, 0,
                         (struct sockaddr *)&req->client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom error");
            free(req);
            continue;
        }

        req->buffer[n] = '\0'; // Null terminate
        req->start_time = time(NULL);
        req->sockfd = sockfd;

        // Assign unique client id
        pthread_mutex_lock(&log_mutex);
        client_counter++;
        req->client_id = client_counter;
        pthread_mutex_unlock(&log_mutex);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_request, req) != 0) {
            perror("Failed to create thread");
            free(req);
            continue;
        }

        pthread_detach(tid);  // Don't need to join threads
    }

    close(sockfd);
    return 0;
}
