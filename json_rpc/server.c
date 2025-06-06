#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h> // Added for pid_t
#include <sys/wait.h>  // Added for waitpid
#include <errno.h>     // Added for errno
#include <getopt.h>    // Added for getopt_long
#include <time.h>      // For logging timestamp
#include <arpa/inet.h> // Added for inet_ntoa and other network functions
#include <sys/select.h> // Added for select()
#include <fcntl.h>     // Added for fcntl O_NONBLOCK

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_BACKENDS 10 // Maximum number of backend processes to manage
#define MAX_REGISTERED_BACKENDS_CONFIG 20 // Max backends registered via discovery
#define GATEWAY_DISCOVERY_HOST "0.0.0.0" // Listen on all interfaces for discovery
#define GATEWAY_DISCOVERY_PORT 8081

// Structure to hold information about a running backend process
typedef struct {
    pid_t pid;
    char name[100];
    char exec_path[256];
    char listen_host[100];
    char listen_port_str[10]; // Store as string
    char server_type[50];
    int is_running; // Flag to indicate if it's supposed to be running
} ManagedBackend;

ManagedBackend managed_backends[MAX_BACKENDS];
int num_managed_backends = 0;

// Structure for discovered backends
typedef struct {
    char type[10]; // "TCP" or "UDP"
    char host[256];
    int port;
    char name[100];
    char operations[512]; // Comma-separated list like "add,subtract,multiply"
    time_t last_seen;
    int is_active; // 1 for active, 0 for inactive
} RegisteredBackend;

RegisteredBackend registered_backends[MAX_REGISTERED_BACKENDS_CONFIG];
int num_registered_backends = 0;
int discovery_fd; // File descriptor for the UDP discovery socket
static unsigned int round_robin_counter = 0; // For round-robin backend selection


// Function for logging with timestamp
void log_with_timestamp(const char *level, const char *message) {
    time_t now = time(NULL);
    char buf[sizeof("YYYY-MM-DD HH:MM:SS")];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    // Output to stdout for now, could be stderr or a file
    printf("[%s] [%s] %s\n", buf, level, message);
}


// Maps JSON-RPC method name to backend operation code
int get_backend_op_code(const char* json_rpc_method) {
    if (strcmp(json_rpc_method, "add") == 0) return 1;
    if (strcmp(json_rpc_method, "subtract") == 0) return 2;
    if (strcmp(json_rpc_method, "multiply") == 0) return 3;
    if (strcmp(json_rpc_method, "divide") == 0) return 4;

    char log_buf[100];
    snprintf(log_buf, sizeof(log_buf), "Unknown JSON-RPC method for op code translation: %s", json_rpc_method);
    log_with_timestamp("WARNING", log_buf);
    return 0; // Unknown or unsupported method
}

// Communicate with a TCP backend
int communicate_with_tcp_backend(const RegisteredBackend* backend, const char* request_payload, char* response_buf, size_t response_buf_size) {
    char log_buf[512];
    snprintf(log_buf, sizeof(log_buf), "Attempting TCP communication with %s at %s:%d. Payload: \"%s\"", backend->name, backend->host, backend->port, request_payload);
    log_with_timestamp("INFO", log_buf);

    int sock_fd;
    struct sockaddr_in server_addr;

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "TCP socket creation for backend %s failed: %s", backend->name, strerror(errno));
        perror("TCP socket creation failed for backend communication"); // System error
        log_with_timestamp("ERROR", err_buf); // Application context
        snprintf(response_buf, response_buf_size -1, "Gateway error: Failed to create TCP socket to backend %s.", backend->name);
        response_buf[response_buf_size -1] = '\0';
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "setsockopt SO_RCVTIMEO for backend %s failed: %s", backend->name, strerror(errno));
        perror("setsockopt SO_RCVTIMEO failed for TCP backend socket");
        log_with_timestamp("ERROR", err_buf);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to set TCP recv timeout for backend %s.", backend->name);
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }
    if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv) < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "setsockopt SO_SNDTIMEO for backend %s failed: %s (non-critical)", backend->name, strerror(errno));
        perror("setsockopt SO_SNDTIMEO failed for TCP backend socket (non-critical)");
        log_with_timestamp("WARNING", err_buf);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(backend->port);
    if (inet_pton(AF_INET, backend->host, &server_addr.sin_addr) <= 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Invalid backend address %s for %s: %s", backend->host, backend->name, strerror(errno));
        perror("Invalid address/ Address not supported for TCP backend");
        log_with_timestamp("ERROR", err_buf);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Invalid backend address %s for %s.", backend->host, backend->name);
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "TCP connect to backend %s (%s:%d) failed: %s", backend->name, backend->host, backend->port, strerror(errno));
        log_with_timestamp("ERROR", err_msg);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to connect to backend %s. Details: %s", backend->name, strerror(errno));
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }
    log_with_timestamp("INFO", "TCP connected to backend.");

    if (send(sock_fd, request_payload, strlen(request_payload), 0) < 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "TCP send to backend %s (%s:%d) failed: %s", backend->name, backend->host, backend->port, strerror(errno));
        log_with_timestamp("ERROR", err_msg);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to send data to backend %s. Details: %s", backend->name, strerror(errno));
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }
    log_with_timestamp("INFO", "TCP data sent to backend.");

    ssize_t bytes_received = recv(sock_fd, response_buf, response_buf_size - 1, 0);
    if (bytes_received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snprintf(log_buf, sizeof(log_buf), "TCP recv from backend %s timed out.", backend->name);
            log_with_timestamp("ERROR", log_buf);
            snprintf(response_buf, response_buf_size-1, "Gateway error: Timeout receiving data from backend %s.", backend->name);
        } else {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "TCP recv from backend %s (%s:%d) failed: %s", backend->name, backend->host, backend->port, strerror(errno));
            log_with_timestamp("ERROR", err_msg);
            snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to receive data from backend %s. Details: %s", backend->name, strerror(errno));
        }
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }
    response_buf[bytes_received] = '\0';
    snprintf(log_buf, sizeof(log_buf), "TCP received from backend %s: %s", backend->name, response_buf);
    log_with_timestamp("INFO", log_buf);

    close(sock_fd);
    return 0;
}

// Communicate with a UDP backend
int communicate_with_udp_backend(const RegisteredBackend* backend, const char* request_payload, char* response_buf, size_t response_buf_size) {
    char log_buf[512];
    snprintf(log_buf, sizeof(log_buf), "Attempting UDP communication with %s at %s:%d. Payload: \"%s\"", backend->name, backend->host, backend->port, request_payload);
    log_with_timestamp("INFO", log_buf);

    int sock_fd;
    struct sockaddr_in server_addr, from_addr;
    socklen_t from_addr_len = sizeof(from_addr);

    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "UDP socket creation for backend %s failed: %s", backend->name, strerror(errno));
        perror("UDP socket creation failed for backend communication");
        log_with_timestamp("ERROR", err_buf);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to create UDP socket for backend %s.", backend->name);
        response_buf[response_buf_size -1] = '\0';
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "setsockopt SO_RCVTIMEO for UDP backend %s failed: %s", backend->name, strerror(errno));
        perror("setsockopt SO_RCVTIMEO failed for UDP backend socket");
        log_with_timestamp("ERROR", err_buf);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to set UDP recv timeout for backend %s.", backend->name);
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(backend->port);
    if (inet_pton(AF_INET, backend->host, &server_addr.sin_addr) <= 0) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Invalid backend address %s for %s: %s", backend->host, backend->name, strerror(errno));
        perror("Invalid address/ Address not supported for UDP backend");
        log_with_timestamp("ERROR", err_buf);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Invalid backend address %s for %s.", backend->host, backend->name);
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }

    if (sendto(sock_fd, request_payload, strlen(request_payload), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "UDP sendto to backend %s (%s:%d) failed: %s", backend->name, backend->host, backend->port, strerror(errno));
        log_with_timestamp("ERROR", err_msg);
        snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to send data to UDP backend %s. Details: %s", backend->name, strerror(errno));
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }
    log_with_timestamp("INFO", "UDP data sent to backend.");

    ssize_t bytes_received = recvfrom(sock_fd, response_buf, response_buf_size - 1, 0, (struct sockaddr *)&from_addr, &from_addr_len);
    if (bytes_received < 0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) {
            snprintf(log_buf, sizeof(log_buf), "UDP recvfrom backend %s timed out.", backend->name);
            log_with_timestamp("ERROR", log_buf);
            snprintf(response_buf, response_buf_size-1, "Gateway error: Timeout receiving data from UDP backend %s.", backend->name);
        } else {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "UDP recvfrom backend %s (%s:%d) failed: %s", backend->name, backend->host, backend->port, strerror(errno));
            log_with_timestamp("ERROR", err_msg);
            snprintf(response_buf, response_buf_size-1, "Gateway error: Failed to receive data from UDP backend %s. Details: %s", backend->name, strerror(errno));
        }
        response_buf[response_buf_size -1] = '\0';
        close(sock_fd);
        return -1;
    }
    response_buf[bytes_received] = '\0';
    snprintf(log_buf, sizeof(log_buf), "UDP received from backend %s: %s", backend->name, response_buf);
    log_with_timestamp("INFO", log_buf);

    close(sock_fd);
    return 0;
}

// Parses the simple "Result: value" or "Error: message" from backend
int parse_backend_response(const char* backend_response_str, double* result_out, char* error_msg_out, size_t error_msg_out_size) {
    if (!backend_response_str || !result_out || !error_msg_out) {
        log_with_timestamp("CRITICAL", "NULL argument to parse_backend_response");
        if(error_msg_out) snprintf(error_msg_out, error_msg_out_size-1, "Gateway internal error: parsing backend response with NULL params.");
        if(error_msg_out && error_msg_out_size > 0) error_msg_out[error_msg_out_size-1] = '\0';
        return -1;
    }

    char log_buf[1024];
    snprintf(log_buf, sizeof(log_buf)-1, "Parsing backend response: \"%s\"", backend_response_str);
    log_with_timestamp("DEBUG", log_buf);

    if (strncmp(backend_response_str, "Result: ", 8) == 0) {
        if (sscanf(backend_response_str + 8, "%lf", result_out) == 1) {
            snprintf(log_buf, sizeof(log_buf)-1, "Parsed result from backend: %f", *result_out);
            log_with_timestamp("DEBUG", log_buf);
            return 0;
        } else {
            snprintf(error_msg_out, error_msg_out_size-1, "Malformed result from backend: %s", backend_response_str);
            error_msg_out[error_msg_out_size-1] = '\0';
            log_with_timestamp("ERROR", error_msg_out);
            return -1;
        }
    } else if (strncmp(backend_response_str, "Error: ", 7) == 0) {
        strncpy(error_msg_out, backend_response_str + 7, error_msg_out_size - 1);
        error_msg_out[error_msg_out_size - 1] = '\0';
        snprintf(log_buf, sizeof(log_buf)-1, "Parsed error from backend: \"%s\"", error_msg_out);
        log_with_timestamp("INFO", log_buf);
        return 1;
    }

    snprintf(error_msg_out, error_msg_out_size-1, "Unknown response format from backend: %s", backend_response_str);
    error_msg_out[error_msg_out_size-1] = '\0';
    log_with_timestamp("ERROR", error_msg_out);
    return -1;
}

int is_operation_supported(const RegisteredBackend* backend, const char* operation_name) {
    if (!backend || !backend->is_active || !operation_name) {
        return 0;
    }
    char ops_copy[sizeof(backend->operations)];
    strncpy(ops_copy, backend->operations, sizeof(ops_copy) -1);
    ops_copy[sizeof(ops_copy)-1] = '\0';

    char *saveptr;
    char *token = strtok_r(ops_copy, ",", &saveptr);
    while (token != NULL) {
        if (strcmp(token, operation_name) == 0) {
            return 1;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    return 0;
}

RegisteredBackend* select_backend(const char* operation_name, char* chosen_backend_name_out, size_t chosen_backend_name_out_size) {
    char log_buf[512];
    if (!operation_name || !chosen_backend_name_out) return NULL;

    RegisteredBackend* candidates[MAX_REGISTERED_BACKENDS_CONFIG];
    int num_candidates = 0;

    for (int i = 0; i < num_registered_backends; ++i) {
        if (is_operation_supported(&registered_backends[i], operation_name)) {
            if (num_candidates < MAX_REGISTERED_BACKENDS_CONFIG) {
                 candidates[num_candidates++] = &registered_backends[i];
            } else {
                log_with_timestamp("WARNING", "Exceeded candidate array capacity in select_backend. This shouldn't happen.");
                break;
            }
        }
    }

    if (num_candidates == 0) {
        snprintf(log_buf, sizeof(log_buf), "No active backend found supporting operation: %s", operation_name);
        log_with_timestamp("WARNING", log_buf);
        strncpy(chosen_backend_name_out, "N/A (No suitable backend)", chosen_backend_name_out_size -1);
        chosen_backend_name_out[chosen_backend_name_out_size-1] = '\0';
        return NULL;
    }

    RegisteredBackend* selected = candidates[round_robin_counter % num_candidates];
    round_robin_counter++;

    strncpy(chosen_backend_name_out, selected->name, chosen_backend_name_out_size -1);
    chosen_backend_name_out[chosen_backend_name_out_size-1] = '\0';

    snprintf(log_buf, sizeof(log_buf), "Selected backend %s for operation %s via round-robin from %d candidates.", selected->name, operation_name, num_candidates);
    log_with_timestamp("INFO", log_buf);

    return selected;
}

void setup_discovery_socket() {
    char log_buf[256];
    struct sockaddr_in discovery_addr;

    discovery_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_fd < 0) {
        perror("Discovery socket creation failed");
        log_with_timestamp("ERROR", "Discovery UDP socket creation failed.");
        exit(EXIT_FAILURE);
    }

    int flags = fcntl(discovery_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed for discovery_fd");
        log_with_timestamp("ERROR", "fcntl F_GETFL failed for discovery_fd.");
        close(discovery_fd);
        exit(EXIT_FAILURE);
    }
    if (fcntl(discovery_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK failed for discovery_fd");
        log_with_timestamp("ERROR", "fcntl F_SETFL O_NONBLOCK failed for discovery_fd.");
        close(discovery_fd);
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(discovery_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&discovery_addr, 0, sizeof(discovery_addr));
    discovery_addr.sin_family = AF_INET;
    if (strcmp(GATEWAY_DISCOVERY_HOST, "0.0.0.0") == 0) {
        discovery_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, GATEWAY_DISCOVERY_HOST, &discovery_addr.sin_addr) <= 0) {
            perror("Invalid GATEWAY_DISCOVERY_HOST address");
            snprintf(log_buf, sizeof(log_buf), "Invalid GATEWAY_DISCOVERY_HOST address: %s", GATEWAY_DISCOVERY_HOST);
            log_with_timestamp("ERROR", log_buf);
            close(discovery_fd);
            exit(EXIT_FAILURE);
        }
    }
    discovery_addr.sin_port = htons(GATEWAY_DISCOVERY_PORT);

    if (bind(discovery_fd, (struct sockaddr *)&discovery_addr, sizeof(discovery_addr)) < 0) {
        perror("Discovery socket bind failed");
        snprintf(log_buf, sizeof(log_buf), "Discovery UDP socket bind failed on %s:%d - %s.", GATEWAY_DISCOVERY_HOST, GATEWAY_DISCOVERY_PORT, strerror(errno));
        log_with_timestamp("ERROR", log_buf);
        close(discovery_fd);
        exit(EXIT_FAILURE);
    }

    snprintf(log_buf, sizeof(log_buf), "Discovery UDP socket listening on %s:%d", GATEWAY_DISCOVERY_HOST, GATEWAY_DISCOVERY_PORT);
    log_with_timestamp("INFO", log_buf);
}

int parse_registration_message(const char* msg, RegisteredBackend* backend_info) {
    if (!msg || !backend_info) return -1;
    char log_buf[1024];
    snprintf(log_buf, sizeof(log_buf), "Parsing registration message: %s", msg);
    log_with_timestamp("DEBUG", log_buf);

    memset(backend_info, 0, sizeof(RegisteredBackend));

    char msg_copy[1024];
    strncpy(msg_copy, msg, sizeof(msg_copy) -1);
    msg_copy[sizeof(msg_copy)-1] = '\0';

    char *saveptr1, *saveptr2;
    char *token = strtok_r(msg_copy, ";", &saveptr1);
    int found_fields = 0;

    while (token != NULL) {
        char *key = strtok_r(token, "=", &saveptr2);
        char *value = strtok_r(NULL, "=", &saveptr2);

        if (key && value) {
            if (strcmp(key, "type") == 0) {
                strncpy(backend_info->type, value, sizeof(backend_info->type) - 1);
                backend_info->type[sizeof(backend_info->type) - 1] = '\0';
                found_fields++;
            } else if (strcmp(key, "host") == 0) {
                strncpy(backend_info->host, value, sizeof(backend_info->host) - 1);
                backend_info->host[sizeof(backend_info->host) - 1] = '\0';
                found_fields++;
            } else if (strcmp(key, "port") == 0) {
                backend_info->port = atoi(value);
                if (backend_info->port == 0 && strcmp(value, "0") != 0) {
                    snprintf(log_buf, sizeof(log_buf), "Invalid port value in registration: %s for key %s", value, key);
                    log_with_timestamp("ERROR", log_buf);
                    return -1;
                }
                found_fields++;
            } else if (strcmp(key, "name") == 0) {
                strncpy(backend_info->name, value, sizeof(backend_info->name) - 1);
                backend_info->name[sizeof(backend_info->name) - 1] = '\0';
                found_fields++;
            } else if (strcmp(key, "ops") == 0) {
                strncpy(backend_info->operations, value, sizeof(backend_info->operations) - 1);
                backend_info->operations[sizeof(backend_info->operations) - 1] = '\0';
                found_fields++;
            }
        }
        token = strtok_r(NULL, ";", &saveptr1);
    }

    if (found_fields < 5) {
        snprintf(log_buf, sizeof(log_buf), "Incomplete registration message. Found %d fields. Original: %s", found_fields, msg);
        log_with_timestamp("ERROR", log_buf);
        return -1;
    }

    if(strlen(backend_info->name) == 0){
        log_with_timestamp("ERROR", "Backend name is empty after parsing.");
        return -1;
    }

    backend_info->last_seen = time(NULL);
    backend_info->is_active = 1;
    return 0;
}

void process_registration_message(const char* buffer, ssize_t len) {
    char log_buf[1024];
    char safe_buffer[1024];
    if (len >= (ssize_t)sizeof(safe_buffer)) {
        log_with_timestamp("ERROR", "Received registration message too long to process.");
        return;
    }
    memcpy(safe_buffer, buffer, len);
    safe_buffer[len] = '\0';

    RegisteredBackend backend_info;
    if (parse_registration_message(safe_buffer, &backend_info) == 0) {
        int found_idx = -1;
        for (int i = 0; i < num_registered_backends; ++i) {
            if (strcmp(registered_backends[i].name, backend_info.name) == 0) {
                found_idx = i;
                break;
            }
        }

        if (found_idx != -1) {
            registered_backends[found_idx] = backend_info;
            snprintf(log_buf, sizeof(log_buf), "Updated registration for backend: %s (Type: %s, Host: %s, Port: %d, Ops: %s)",
                     backend_info.name, backend_info.type, backend_info.host, backend_info.port, backend_info.operations);
            log_with_timestamp("INFO", log_buf);
        } else {
            if (num_registered_backends < MAX_REGISTERED_BACKENDS_CONFIG) {
                registered_backends[num_registered_backends++] = backend_info;
                snprintf(log_buf, sizeof(log_buf), "Registered new backend: %s (Type: %s, Host: %s, Port: %d, Ops: %s)",
                         backend_info.name, backend_info.type, backend_info.host, backend_info.port, backend_info.operations);
                log_with_timestamp("INFO", log_buf);
            } else {
                snprintf(log_buf, sizeof(log_buf), "Cannot register backend %s: list full (max %d).", backend_info.name, MAX_REGISTERED_BACKENDS_CONFIG);
                log_with_timestamp("WARNING", log_buf);
            }
        }
    } else {
        snprintf(log_buf, sizeof(log_buf), "Failed to parse registration message: %s", safe_buffer);
        log_with_timestamp("ERROR", log_buf);
    }
}

double add(double a, double b);
double subtract(double a, double b);
double multiply(double a, double b);
double divide(double a, double b);

int parse_json_rpc_request(const char *json_str, char *method, double *params, int *id);
void build_json_rpc_response(char *response_str, int id, double result, const char *error_message);

pid_t launch_backend(const char* exec_path, const char* server_name, const char* listen_host, const char* listen_port_str, const char* server_type) {
    char log_buffer[512];
    snprintf(log_buffer, sizeof(log_buffer), "Attempting to launch backend: %s (Name: %s, Host: %s, Port: %s, Type: %s)",
             exec_path, server_name, listen_host, listen_port_str, server_type);
    log_with_timestamp("INFO", log_buffer);

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        snprintf(log_buffer, sizeof(log_buffer), "Failed to fork for backend %s: %s", server_name, strerror(errno));
        log_with_timestamp("ERROR", log_buffer);
        return 0;
    } else if (pid == 0) {
        char gateway_port_str[10];
        snprintf(gateway_port_str, sizeof(gateway_port_str), "%d", GATEWAY_DISCOVERY_PORT);

        char *argv[] = {
            (char*)exec_path,
            "--my-host", (char*)listen_host,
            "--my-port", (char*)listen_port_str,
            "--server-name", (char*)server_name,
            "--gateway-host", GATEWAY_DISCOVERY_HOST, // This should be the routable IP of the gateway if backends are on different machines
            "--gateway-port", gateway_port_str,
            NULL
        };

        snprintf(log_buffer, sizeof(log_buffer), "Child process for %s executing: %s --my-host %s --my-port %s --server-name %s --gateway-host %s --gateway-port %s",
            server_name, exec_path, listen_host, listen_port_str, server_name, GATEWAY_DISCOVERY_HOST, gateway_port_str);
        log_with_timestamp("DEBUG", log_buffer);

        execv(exec_path, argv);
        perror("execv failed");
        snprintf(log_buffer, sizeof(log_buffer), "execv failed for %s: %s", exec_path, strerror(errno));
        log_with_timestamp("ERROR", log_buffer);
        exit(EXIT_FAILURE);
    } else {
        snprintf(log_buffer, sizeof(log_buffer), "Backend %s launched successfully with PID: %d", server_name, pid);
        log_with_timestamp("INFO", log_buffer);
        return pid;
    }
}

void load_and_launch_backends(const char* config_path) {
    char log_buffer[512];
    snprintf(log_buffer, sizeof(log_buffer), "Loading backends configuration from: %s", config_path);
    log_with_timestamp("INFO", log_buffer);

    FILE *file = fopen(config_path, "r");
    if (!file) {
        perror("fopen config_path failed");
        snprintf(log_buffer, sizeof(log_buffer), "Failed to open backend config file %s: %s. No managed backends will be launched.", config_path, strerror(errno));
        log_with_timestamp("ERROR", log_buffer);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (num_managed_backends >= MAX_BACKENDS) {
            log_with_timestamp("WARNING", "Maximum number of managed backends reached. Skipping remaining entries in backends.conf.");
            break;
        }
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0 || line[0] == '#') continue; // Skip empty or comment lines


        char exec_path[256], server_name[100], listen_host[100], listen_port_str[10], server_type[50];

        if (sscanf(line, "%255s %99s %99s %9s %49s", exec_path, server_name, listen_host, listen_port_str, server_type) == 5) {
            pid_t pid = launch_backend(exec_path, server_name, listen_host, listen_port_str, server_type);
            if (pid > 0) {
                ManagedBackend *backend = &managed_backends[num_managed_backends++];
                backend->pid = pid;
                strncpy(backend->name, server_name, sizeof(backend->name) - 1);
                backend->name[sizeof(backend->name) - 1] = '\0';
                strncpy(backend->exec_path, exec_path, sizeof(backend->exec_path) -1);
                backend->exec_path[sizeof(backend->exec_path) -1] = '\0';
                strncpy(backend->listen_host, listen_host, sizeof(backend->listen_host) -1);
                backend->listen_host[sizeof(backend->listen_host) -1] = '\0';
                strncpy(backend->listen_port_str, listen_port_str, sizeof(backend->listen_port_str) -1);
                backend->listen_port_str[sizeof(backend->listen_port_str) -1] = '\0';
                strncpy(backend->server_type, server_type, sizeof(backend->server_type) -1);
                backend->server_type[sizeof(backend->server_type) -1] = '\0';
                backend->is_running = 1;
            } else {
                snprintf(log_buffer, sizeof(log_buffer), "Failed to launch backend defined in line: %s", line);
                log_with_timestamp("ERROR", log_buffer);
            }
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "Skipping malformed line in backend config: %s", line);
            log_with_timestamp("WARNING", log_buffer);
        }
    }
    fclose(file);
    snprintf(log_buffer, sizeof(log_buffer), "Finished loading backends. Total managed backends launched: %d", num_managed_backends);
    log_with_timestamp("INFO", log_buffer);
}

void check_managed_backends() {
    char log_buffer[256];
    for (int i = 0; i < num_managed_backends; ++i) {
        if (managed_backends[i].pid > 0 && managed_backends[i].is_running) {
            int status;
            pid_t result = waitpid(managed_backends[i].pid, &status, WNOHANG);
            if (result == managed_backends[i].pid) {
                if (WIFEXITED(status)) {
                    snprintf(log_buffer, sizeof(log_buffer), "Managed backend %s (PID: %d) exited with status %d.",
                             managed_backends[i].name, managed_backends[i].pid, WEXITSTATUS(status));
                    log_with_timestamp("INFO", log_buffer);
                } else if (WIFSIGNALED(status)) {
                    snprintf(log_buffer, sizeof(log_buffer), "Managed backend %s (PID: %d) killed by signal %d.",
                             managed_backends[i].name, managed_backends[i].pid, WTERMSIG(status));
                    log_with_timestamp("WARNING", log_buffer);
                }
                managed_backends[i].is_running = 0;
                log_with_timestamp("INFO", "Attempting to relaunch backend...");
                pid_t new_pid = launch_backend(managed_backends[i].exec_path, managed_backends[i].name, managed_backends[i].listen_host, managed_backends[i].listen_port_str, managed_backends[i].server_type);
                if (new_pid > 0) {
                    managed_backends[i].pid = new_pid;
                    managed_backends[i].is_running = 1;
                    snprintf(log_buffer, sizeof(log_buffer), "Backend %s re-launched with new PID: %d", managed_backends[i].name, new_pid);
                    log_with_timestamp("INFO", log_buffer);
                } else {
                    snprintf(log_buffer, sizeof(log_buffer), "Failed to re-launch backend %s.", managed_backends[i].name);
                    log_with_timestamp("ERROR", log_buffer);
                }

            } else if (result == -1) {
                perror("waitpid error checking managed backend");
                snprintf(log_buffer, sizeof(log_buffer), "Error checking status of managed backend %s (PID: %d): %s",
                         managed_backends[i].name, managed_backends[i].pid, strerror(errno));
                log_with_timestamp("ERROR", log_buffer);
                managed_backends[i].is_running = 0;
            }
        }
    }
}


int main() {
    load_and_launch_backends("json_rpc/backends.conf");

    if (num_managed_backends == 0) {
        log_with_timestamp("WARNING", "CRITICAL SETUP: No backends were successfully launched from backends.conf. Gateway may not be able to process any backend requests that rely on these managed backends.");
    }

    setup_discovery_socket();

    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    char log_buf[512];

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("TCP socket failed");
        log_with_timestamp("CRITICAL", "JSON-RPC TCP Socket creation failed. Exiting.");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt for TCP socket failed");
        log_with_timestamp("CRITICAL", "setsockopt for JSON-RPC TCP socket failed. Exiting.");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(DEFAULT_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("TCP bind failed");
        snprintf(log_buf, sizeof(log_buf), "JSON-RPC TCP Bind failed for port %d: %s. Exiting.", DEFAULT_PORT, strerror(errno));
        log_with_timestamp("CRITICAL", log_buf);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("TCP listen failed");
        log_with_timestamp("CRITICAL", "JSON-RPC TCP Listen failed. Exiting.");
        exit(EXIT_FAILURE);
    }

    snprintf(log_buf, sizeof(log_buf), "JSON-RPC Server listening on port %d", DEFAULT_PORT);
    log_with_timestamp("INFO", log_buf);

    while(1) {
        check_managed_backends();

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        FD_SET(discovery_fd, &readfds);

        int max_fd = (server_fd > discovery_fd ? server_fd : discovery_fd) + 1;

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(max_fd, &readfds, NULL, NULL, &timeout);

        if (activity < 0 && errno != EINTR) {
            char err_select_buf[100];
            snprintf(err_select_buf, sizeof(err_select_buf), "Select error: %s. Continuing...", strerror(errno));
            perror("select error"); // This will print the system error
            log_with_timestamp("ERROR", err_select_buf); // Log our formatted error
            // Depending on the error, the server might continue or exit.
            // For now, we continue, which is suitable for some transient errors.
            // If select consistently fails, the server might become unresponsive.
            // Consider adding a counter for consecutive select failures to exit eventually.
            continue;
        }

        if (activity == 0) {
            continue;
        }

        if (FD_ISSET(discovery_fd, &readfds)) {
            char reg_buffer[1024];
            struct sockaddr_in backend_client_addr;
            socklen_t backend_addr_len = sizeof(backend_client_addr);

            ssize_t len = recvfrom(discovery_fd, reg_buffer, sizeof(reg_buffer) - 1, 0,
                                   (struct sockaddr*)&backend_client_addr, &backend_addr_len);

            if (len > 0) {
                reg_buffer[len] = '\0';
                char client_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &backend_client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
                snprintf(log_buf, sizeof(log_buf), "Received registration message from %s:%d : %s",
                         client_ip_str, ntohs(backend_client_addr.sin_port), reg_buffer);
                log_with_timestamp("INFO", log_buf);
                process_registration_message(reg_buffer, len);
            } else if (len < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "Error receiving from discovery UDP socket: %s", strerror(errno));
                perror("recvfrom discovery_fd error");
                log_with_timestamp("ERROR", err_msg);
            }
        }

        if (FD_ISSET(server_fd, &readfds)) {
            log_with_timestamp("INFO", "Activity on JSON-RPC socket. Waiting for a new connection...");
            if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "Accept for JSON-RPC failed: %s", strerror(errno));
                perror("accept for JSON-RPC failed");
                log_with_timestamp("ERROR", err_msg);
                continue;
            }
            log_with_timestamp("INFO","JSON-RPC Connection accepted from a client.");

            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);

            if (bytes_read < 0) {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "Read from JSON-RPC client failed: %s", strerror(errno));
                perror("read from JSON-RPC client failed");
                log_with_timestamp("ERROR", err_msg);
                close(client_fd);
                continue;
            }
            if (bytes_read == 0) {
                log_with_timestamp("INFO", "JSON-RPC client disconnected gracefully (read 0 bytes).");
                close(client_fd);
                continue;
            }
            buffer[bytes_read] = '\0';
            snprintf(log_buf, sizeof(log_buf), "Received JSON-RPC request: %s", buffer);
            log_with_timestamp("DEBUG", log_buf);

            char method[256];
            double params[2];
            int id = -1;
            int parse_status = parse_json_rpc_request(buffer, method, params, &id);
            char response_str[BUFFER_SIZE];
            memset(response_str, 0, BUFFER_SIZE);

            if (parse_status == 0) {
                const char *error_message = NULL;

                char chosen_backend_name[100] = "N/A";
                RegisteredBackend* selected_backend = select_backend(method, chosen_backend_name, sizeof(chosen_backend_name));

                if (selected_backend == NULL) {
                    snprintf(log_buf, sizeof(log_buf), "Method '%s' (id: %d) not supported by any available backend or no backends available.", method, id);
                    log_with_timestamp("ERROR", log_buf);
                    error_message = "Method not supported by any available backend or no backends available.";
                    build_json_rpc_response(response_str, id, 0.0, error_message);
                } else {
                    snprintf(log_buf, sizeof(log_buf), "Routing request for method '%s' (id: %d) to backend: %s (%s:%d)",
                             method, id, selected_backend->name, selected_backend->host, selected_backend->port);
                    log_with_timestamp("INFO", log_buf);

                    int op_code = get_backend_op_code(method);
                    if (op_code == 0) {
                        error_message = "Internal server error: Method mapped to unknown operation code.";
                        snprintf(log_buf, sizeof(log_buf), "Method '%s' (id: %d) mapped to op_code 0. This indicates an issue with is_operation_supported or get_backend_op_code logic.", method, id);
                        log_with_timestamp("CRITICAL", log_buf);
                        build_json_rpc_response(response_str, id, 0.0, error_message);
                    } else {
                        char backend_request_str[256];
                        sprintf(backend_request_str, "%d %lf %lf", op_code, params[0], params[1]);

                        char backend_response_str[1024] = {0};
                        int communication_status = -1;

                        if (strcmp(selected_backend->type, "TCP") == 0) {
                            communication_status = communicate_with_tcp_backend(selected_backend, backend_request_str, backend_response_str, sizeof(backend_response_str));
                        } else if (strcmp(selected_backend->type, "UDP") == 0) {
                            communication_status = communicate_with_udp_backend(selected_backend, backend_request_str, backend_response_str, sizeof(backend_response_str));
                        } else {
                            snprintf(log_buf, sizeof(log_buf), "Unknown backend type '%s' for backend %s (id: %d)", selected_backend->type, selected_backend->name, id);
                            log_with_timestamp("ERROR", log_buf);
                            error_message = "Internal server error: Unknown backend type configured.";
                        }

                        if (error_message == NULL) {
                            double backend_result = 0.0;
                            char backend_error_msg[BUFFER_SIZE] = {0};
                            const char* final_error_message_ptr = NULL;

                            if (communication_status != 0) {
                                snprintf(log_buf, sizeof(log_buf), "Error communicating with backend %s (id: %d): %s", selected_backend->name, id, backend_response_str);
                                log_with_timestamp("ERROR", log_buf);
                                final_error_message_ptr = backend_response_str;
                            } else {
                                snprintf(log_buf, sizeof(log_buf), "Raw response from backend %s (id: %d): \"%s\"", selected_backend->name, id, backend_response_str);
                                log_with_timestamp("INFO", log_buf);
                                int parse_res_status = parse_backend_response(backend_response_str, &backend_result, backend_error_msg, sizeof(backend_error_msg));

                                if (parse_res_status == 0) {
                                } else if (parse_res_status == 1) {
                                    final_error_message_ptr = backend_error_msg;
                                } else {
                                    final_error_message_ptr = backend_error_msg;
                                }
                            }
                            build_json_rpc_response(response_str, id, backend_result, final_error_message_ptr);
                        } else {
                             build_json_rpc_response(response_str, id, 0.0, error_message);
                        }
                    }
                }
            } else {
                snprintf(log_buf, sizeof(log_buf), "Failed to parse JSON-RPC request (id: %d). Body: %s", id, buffer);
                log_with_timestamp("ERROR", log_buf);
                build_json_rpc_response(response_str, id, 0.0, "Parse error. Invalid JSON-RPC request.");
            }

            snprintf(log_buf, sizeof(log_buf), "Sending JSON-RPC response (id: %d): %s", id, response_str);
            log_with_timestamp("DEBUG", log_buf);
            if (write(client_fd, response_str, strlen(response_str)) < 0) {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "Write to JSON-RPC client failed (id: %d): %s", id, strerror(errno));
                perror("write to JSON-RPC client failed");
                log_with_timestamp("ERROR", err_msg);
            }
            close(client_fd);
            log_with_timestamp("INFO", "JSON-RPC Connection closed.");
        }
    }

    log_with_timestamp("INFO", "Shutting down server.");
    close(server_fd);
    close(discovery_fd);
    return 0;
}

double add(double a, double b) { return a + b; }
double subtract(double a, double b) { return a - b; }
double multiply(double a, double b) { return a * b; }
double divide(double a, double b) { return a / b; } // Basic, assumes caller checks for b==0 based on error_message

int parse_json_rpc_request(const char *json_str, char *method, double *params, int *id) {
    if (json_str == NULL || method == NULL || params == NULL || id == NULL) {
        log_with_timestamp("CRITICAL", "NULL argument to parse_json_rpc_request");
        return -1;
    }
    *id = -1;

    char log_buf[BUFFER_SIZE + 50];
    const char *method_key = "\"method\": \"";
    const char *method_start = strstr(json_str, method_key);
    if (method_start == NULL) {
        snprintf(log_buf, sizeof(log_buf), "Parse error: method key not found in request: %.1000s", json_str);
        log_with_timestamp("ERROR", log_buf);
        return -1;
    }
    method_start += strlen(method_key);
    const char *method_end = strchr(method_start, '\"');
    if (method_end == NULL || (size_t)(method_end - method_start) >= 256 ) {
        snprintf(log_buf, sizeof(log_buf), "Parse error: method value not found or too long in request: %.1000s", json_str);
        log_with_timestamp("ERROR", log_buf);
        return -1;
    }
    strncpy(method, method_start, method_end - method_start);
    method[method_end - method_start] = '\0';

    const char *params_key = "\"params\": [";
    const char *params_start = strstr(json_str, params_key);
    if (params_start == NULL) {
        snprintf(log_buf, sizeof(log_buf), "Parse error: params key not found for method '%s' in request: %.1000s", method, json_str);
        log_with_timestamp("ERROR", log_buf);
        return -1;
    }
    params_start += strlen(params_key);
    if (sscanf(params_start, "%lf, %lf]", &params[0], &params[1]) != 2 &&
        sscanf(params_start, "%lf,%lf]", &params[0], &params[1]) != 2) {
        snprintf(log_buf, sizeof(log_buf), "Parse error: could not parse params array for method '%s' in request: %.1000s", method, json_str);
        log_with_timestamp("ERROR", log_buf);
        return -1;
    }

    const char *id_key = "\"id\": ";
    const char *id_start = strstr(json_str, id_key);
    if (id_start == NULL) {
        snprintf(log_buf, sizeof(log_buf), "Parse error: id key not found for method '%s'. Request: %.1000s", method, json_str);
        log_with_timestamp("ERROR", log_buf);
        return -1;
    }
    id_start += strlen(id_key);
    if (sscanf(id_start, "%d", id) != 1) {
        char temp_id_str[128];
        if (sscanf(id_start, "\"%127[^\"]\"", temp_id_str) == 1) {
             snprintf(log_buf, sizeof(log_buf), "Parse warning: id is a string (\"%s\") but only integer IDs are supported. Method '%s'. Treating as parse error for ID. Request: %.1000s", temp_id_str, method, json_str);
             log_with_timestamp("WARNING", log_buf);
        } else if (strncmp(id_start, "null", 4) == 0) {
             snprintf(log_buf, sizeof(log_buf), "Parse warning: id is null. Method '%s'. Treating as parse error for ID. Request: %.1000s", method, json_str);
             log_with_timestamp("WARNING", log_buf);
        } else {
            snprintf(log_buf, sizeof(log_buf), "Parse error: could not parse id as integer for method '%s'. Request: %.1000s", method, json_str);
            log_with_timestamp("ERROR", log_buf);
        }
        *id = -1; // Ensure id is -1 on any parsing failure for it
        return -1;
    }
    return 0;
}

void build_json_rpc_response(char *response_str, int id, double result, const char *error_message) {
    if (response_str == NULL) {
        log_with_timestamp("CRITICAL", "build_json_rpc_response called with NULL response_str");
        return;
    }
    char temp_buf[BUFFER_SIZE];

    if (error_message) {
        char escaped_error_message[sizeof(temp_buf)];
        char *p_esc = escaped_error_message;
        const char *p_err = error_message;
        size_t available_space = sizeof(escaped_error_message) -1;

        while (*p_err && available_space > 0) {
            if (*p_err == '"') {
                if (available_space < 2) break;
                *p_esc++ = '\\';
                *p_esc++ = '"';
                available_space -= 2;
            } else if (*p_err == '\\') {
                 if (available_space < 2) break;
                *p_esc++ = '\\';
                *p_esc++ = '\\';
                available_space -= 2;
            }
            else {
                *p_esc++ = *p_err;
                available_space--;
            }
            p_err++;
        }
        *p_esc = '\0';

        if (id == -1) {
            snprintf(temp_buf, sizeof(temp_buf), "{\"jsonrpc\": \"2.0\", \"error\": {\"code\": -32700, \"message\": \"%s\"}, \"id\": null}", escaped_error_message);
        } else {
            snprintf(temp_buf, sizeof(temp_buf), "{\"jsonrpc\": \"2.0\", \"error\": {\"code\": -32000, \"message\": \"%s\"}, \"id\": %d}", escaped_error_message, id);
        }
    } else {
         if (id == -1) {
            log_with_timestamp("WARNING", "Building successful JSON-RPC response but request ID was -1 (likely parse error). Sending id as null.");
            snprintf(temp_buf, sizeof(temp_buf), "{\"jsonrpc\": \"2.0\", \"result\": %f, \"id\": null}", result);
         } else {
            snprintf(temp_buf, sizeof(temp_buf), "{\"jsonrpc\": \"2.0\", \"result\": %f, \"id\": %d}", result, id);
         }
    }
    strncpy(response_str, temp_buf, BUFFER_SIZE -1);
    response_str[BUFFER_SIZE -1] = '\0';
}
