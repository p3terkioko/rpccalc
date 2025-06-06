#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024

// Calculator functions
double add(double a, double b);
double subtract(double a, double b);
double multiply(double a, double b);
double divide(double a, double b);

// JSON parsing and building functions
int parse_json_rpc_request(const char *json_str, char *method, double *params, int *id);
void build_json_rpc_response(char *response_str, int id, double result, const char *error_message);

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(DEFAULT_PORT);

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", DEFAULT_PORT);

    // Main server loop to accept and handle connections
    while(1) {
        printf("Waiting for a new connection...\n");
        // Accept an incoming connection
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed"); // Clarified error message
            // Depending on the error, the server might continue or exit.
            // For critical errors like EBADF, ENOTSOCK, EOPNOTSUPP, it might be better to exit.
            // For now, we continue, which is suitable for errors like EINTR.
            continue;
        }

        printf("Connection accepted from a client.\n");

        // Read data from the client into the buffer
        // BUFFER_SIZE - 1 to leave space for null terminator
        memset(buffer, 0, BUFFER_SIZE); // Clear buffer before reading
        ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);

        if (bytes_read < 0) {
            perror("read from client failed");
            close(client_fd);
            continue; // Move to next connection
        }

        if (bytes_read == 0) { // Client closed connection
            printf("Client disconnected gracefully (sent 0 bytes).\n");
            close(client_fd);
            continue;
        }

        buffer[bytes_read] = '\0'; // Null-terminate the received string to treat it as a C string
        printf("Received request: %s\n", buffer);

        // Variables to store parsed request components
        char method[256]; // Buffer for method name
        double params[2];   // Array for parameters (assuming two doubles for calculator functions)
        int id = -1;        // Request ID, initialized to -1 (common for "unknown" ID)

        // Attempt to parse the JSON-RPC request from the buffer
        int parse_status = parse_json_rpc_request(buffer, method, params, &id);

        char response_str[BUFFER_SIZE]; // Buffer for the JSON response string
        memset(response_str, 0, BUFFER_SIZE); // Clear response buffer

        if (parse_status == 0) {
            // Successfully parsed the request
            double result = 0.0;
            const char *error_message = NULL; // No error initially

            // Log the parsed request details
            printf("Parsed request: method='%s', params=[%f, %f], id=%d\n", method, params[0], params[1], id);

            // Call appropriate calculator function based on the parsed method
            if (strcmp(method, "add") == 0) {
                result = add(params[0], params[1]);
            } else if (strcmp(method, "subtract") == 0) {
                result = subtract(params[0], params[1]);
            } else if (strcmp(method, "multiply") == 0) {
                result = multiply(params[0], params[1]);
            } else if (strcmp(method, "divide") == 0) {
                if (params[1] == 0.0) { // Check for division by zero explicitly
                    error_message = "Division by zero";
                } else {
                    result = divide(params[0], params[1]);
                }
            } else {
                // Method name does not match any implemented function
                error_message = "Method not found";
            }

            // Build the JSON-RPC response (success or error from method execution)
            build_json_rpc_response(response_str, id, result, error_message);

        } else {
            // Parsing failed; build an error response.
            // The 'id' might not be parsed correctly if the JSON is malformed.
            // If parse_status is an error, 'id' might hold the initialized -1 or a partially parsed value.
            // JSON-RPC spec suggests responding with null id if request id cannot be determined.
            // Our parser sets id to -1 if it can't find/parse it.
            fprintf(stderr, "Failed to parse JSON-RPC request. Request body: %s\n", buffer);
            // The 'id' from parse_json_rpc_request will be used; if it wasn't parsed, it remains -1.
            build_json_rpc_response(response_str, id, 0.0, "Parse error. Invalid JSON-RPC request.");
        }

        printf("Sending response: %s\n", response_str);
        // Send the response string back to the client
        if (write(client_fd, response_str, strlen(response_str)) < 0) {
            perror("write to client failed");
        }

        // Close the client socket after sending the response
        close(client_fd);
        printf("Connection closed for the client.\n\n");
        // Buffer is cleared at the start of the loop for the next request.
    }

    // Close the listening server socket (this part is typically not reached in a simple infinite loop server without signal handling)
    printf("Shutting down server.\n"); // This line is more aspirational without signal handling
    close(server_fd);
    return 0;
}

// Basic calculator function implementations
// These are simple arithmetic operations.
double add(double a, double b) {
    return a + b;
}

double subtract(double a, double b) {
    return a - b;
}

double multiply(double a, double b) {
    return a * b;
}

double divide(double a, double b) {
    // Error handling for division by zero will be in the main logic
    return a / b;
}

// Implementation for parse_json_rpc_request
// This is a simplified parser. A robust parser would ideally use a dedicated JSON library.
// It assumes a well-structured request like:
// {"jsonrpc": "2.0", "method": "subtract", "params": [42.0, 23.0], "id": 1}
// Or for notifications (id may be absent, or null - this parser expects an int id or fails):
// {"jsonrpc": "2.0", "method": "update", "params": [1,2,3,4,5]}
int parse_json_rpc_request(const char *json_str, char *method, double *params, int *id) {
    if (json_str == NULL || method == NULL || params == NULL || id == NULL) {
        return -1; // Null pointers passed
    }

    // Find "method"
    const char *method_key = "\"method\": \"";
    const char *method_start = strstr(json_str, method_key);
    if (method_start == NULL) {
        fprintf(stderr, "Parse error: method key not found\n");
        return -1;
    }
    method_start += strlen(method_key);
    const char *method_end = strchr(method_start, '\"');
    if (method_end == NULL || (method_end - method_start) >= 256) { // 256 is buffer size for method
        fprintf(stderr, "Parse error: method value not found or too long\n");
        return -1;
    }
    strncpy(method, method_start, method_end - method_start);
    method[method_end - method_start] = '\0';

    // Find "params"
    const char *params_key = "\"params\": [";
    const char *params_start = strstr(json_str, params_key);
    if (params_start == NULL) {
        fprintf(stderr, "Parse error: params key not found\n");
        return -1;
    }
    params_start += strlen(params_key);
    // Use sscanf to parse the two double parameters
    // This is fragile; it assumes exactly two numbers and specific formatting.
    if (sscanf(params_start, "%lf, %lf]", &params[0], &params[1]) != 2) {
        // Try parsing with optional space after comma
        if (sscanf(params_start, "%lf, %lf]", &params[0], &params[1]) != 2 && sscanf(params_start, "%lf,%lf]", &params[0], &params[1]) !=2) {
            fprintf(stderr, "Parse error: could not parse params array\n");
            return -1;
        }
    }

    // Find "id"
    // It could be at the end or before/after other fields.
    // This parser assumes id is an integer. JSON-RPC allows string or null ids too.
    const char *id_key = "\"id\": ";
    const char *id_start = strstr(json_str, id_key);
    if (id_start == NULL) {
        // Check if it's a notification (id is optional or null)
        // This basic parser will consider missing ID an error for simplicity,
        // as the main loop expects an ID to build a response.
        // A more robust server might handle notifications differently (no response).
        fprintf(stderr, "Parse error: id key not found\n");
        return -1;
    }
    id_start += strlen(id_key);
    // Use sscanf to parse the integer ID
    if (sscanf(id_start, "%d", id) != 1) {
        fprintf(stderr, "Parse error: could not parse id\n");
        return -1;
    }

    return 0; // Success
}

// Implementation for build_json_rpc_response
void build_json_rpc_response(char *response_str, int id, double result, const char *error_message) {
    if (response_str == NULL) return;

    if (error_message) {
        // According to JSON-RPC 2.0 spec, error object has code and message
        // Using a generic error code for simplicity here.
        // "message" should be a string, so needs to be in quotes.
        sprintf(response_str, "{\"jsonrpc\": \"2.0\", \"error\": {\"code\": -32000, \"message\": \"%s\"}, \"id\": %d}", error_message, id);
    } else {
        // Result also needs to follow JSON format (number for result here)
        sprintf(response_str, "{\"jsonrpc\": \"2.0\", \"result\": %f, \"id\": %d}", result, id);
    }
}
