#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h> // For inet_addr, inet_pton
#include <unistd.h>    // For read, write, close
#include <json-c/json.h> // Main JSON-C header. Should pull in most necessary definitions.

// To compile: gcc client.c -o client -ljson-c

#define SERVER_ADDRESS "127.0.0.1" // IP address of the JSON-RPC server
#define SERVER_PORT 8080           // Port number of the JSON-RPC server
#define BUFFER_SIZE 1024           // Max size for request and response buffers
#define ERROR_BUFFER_SIZE 256      // Max size for error message strings

// Global request ID, incremented for each new request.
// Simplifies ID management for this basic client.
static int request_id = 1;

// Core function to send a JSON-RPC request and receive the server's response.
// request_str: The JSON-RPC request string to send.
// response_buf: Buffer to store the server's response.
// response_buf_size: Size of the response_buf.
// Returns 0 on success, -1 on failure (connection, send, or receive error).
int send_rpc_request(const char *request_str, char *response_buf, size_t response_buf_size);

// Wrapper functions for specific calculator operations.
// These construct the JSON-RPC request, call send_rpc_request, and parse the response.
// n1, n2: The numbers to operate on.
// result: Pointer to store the successful result.
// error_buf: Buffer to store error messages if the call fails.
// error_buf_size: Size of the error_buf.
// Returns 0 if the RPC call was successful and result is obtained, -1 otherwise.
int rpc_add(double n1, double n2, double *result, char *error_buf, size_t error_buf_size);
int rpc_subtract(double n1, double n2, double *result, char *error_buf, size_t error_buf_size);
int rpc_multiply(double n1, double n2, double *result, char *error_buf, size_t error_buf_size);
int rpc_divide(double n1, double n2, double *result, char *error_buf, size_t error_buf_size);

// Parses the JSON-RPC response string.
// response_str: The JSON response string received from the server.
// result_val: Pointer to store the extracted result if the call was successful.
// error_msg_buf: Buffer to store an error message if the response indicates an error or if parsing fails.
// error_msg_buf_size: Size of the error_msg_buf.
// expected_id: The request ID that this response should correspond to.
// Returns 0 if successful and result is extracted, -1 if an error is found or parsing fails.
int parse_rpc_response(const char *response_str, double *result_val, char *error_msg_buf, size_t error_msg_buf_size, int expected_id);


int main() {
    char operation[10]; // Buffer for user input operation
    double num1, num2, result; // Variables for numbers and result
    char error_message[ERROR_BUFFER_SIZE]; // Buffer for error messages from RPC calls

    printf("JSON-RPC Calculator Client\n");
    printf("Available operations: add, sub, mul, div, exit\n");
    printf("Note: For this client to compile and run, 'libjson-c-dev' is required.\n");
    printf("You can typically install it with: sudo apt-get install libjson-c-dev\n\n");

    // Main loop to get user input and perform RPC calls
    while (1) {
        printf("> "); // Prompt
        if (scanf("%s", operation) != 1) {
            // Clear input buffer in case of invalid input (e.g., EOF or read error)
            while (getchar() != '\n' && !feof(stdin) && !ferror(stdin));
            printf("Invalid input format. Please type an operation name.\n");
            continue;
        }

        // Check for 'exit' command
        if (strcmp(operation, "exit") == 0) {
            printf("Exiting client.\n");
            break;
        }

        // Handle calculator operations
        if (strcmp(operation, "add") == 0 || strcmp(operation, "sub") == 0 ||
            strcmp(operation, "mul") == 0 || strcmp(operation, "div") == 0) {

            printf("Enter two numbers (e.g., 10 5): ");
            if (scanf("%lf %lf", &num1, &num2) != 2) {
                while (getchar() != '\n' && !feof(stdin) && !ferror(stdin)); // Clear malformed input line
                printf("Invalid numbers entered. Please enter two numeric values.\n");
                continue;
            }

            error_message[0] = '\0'; // Clear previous error message before new RPC call
            int rpc_call_status = -1; // Default to error status

            // Dispatch to the appropriate RPC wrapper function
            if (strcmp(operation, "add") == 0) {
                rpc_call_status = rpc_add(num1, num2, &result, error_message, ERROR_BUFFER_SIZE);
            } else if (strcmp(operation, "sub") == 0) {
                rpc_call_status = rpc_subtract(num1, num2, &result, error_message, ERROR_BUFFER_SIZE);
            } else if (strcmp(operation, "mul") == 0) {
                rpc_call_status = rpc_multiply(num1, num2, &result, error_message, ERROR_BUFFER_SIZE);
            } else if (strcmp(operation, "div") == 0) {
                rpc_call_status = rpc_divide(num1, num2, &result, error_message, ERROR_BUFFER_SIZE);
            }

            // Print result or error
            if (rpc_call_status == 0) {
                printf("Result: %f\n", result);
            } else {
                // error_message should be populated by the rpc_... function or parse_rpc_response
                printf("Error: %s\n", strlen(error_message) > 0 ? error_message : "Unknown RPC error or connection failure.");
            }

        } else {
            printf("Unknown operation: '%s'. Type 'add', 'sub', 'mul', 'div', or 'exit'.\n", operation);
        }

        // Consume any leftover characters in the input buffer (like the newline after numbers)
        // to prevent issues with the next scanf("%s", operation)
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
    }

    return 0;
}

// Implementation of send_rpc_request: Establishes connection, sends request, receives response.
int send_rpc_request(const char *request_str, char *response_buf, size_t response_buf_size) {
    int sock_fd; // Socket file descriptor
    struct sockaddr_in server_addr; // Server address structure

    // Create a new socket: AF_INET for IPv4, SOCK_STREAM for TCP
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("Client: socket creation failed");
        return -1;
    }

    // Zero out the server address structure
    memset(&server_addr, 0, sizeof(server_addr));

    // Configure server address
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(SERVER_PORT); // Server port (host-to-network short)

    // Convert IP address from text to binary form using inet_pton
    if (inet_pton(AF_INET, SERVER_ADDRESS, &server_addr.sin_addr) <= 0) {
        perror("Client: inet_pton failed for server address");
        close(sock_fd);
        return -1;
    }

    // Connect to the server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Client: connect failed");
        close(sock_fd);
        return -1;
    }

    // Send the JSON-RPC request string to the server
    if (send(sock_fd, request_str, strlen(request_str), 0) < 0) {
        perror("Client: send failed");
        close(sock_fd);
        return -1;
    }

    // Receive the server's response
    memset(response_buf, 0, response_buf_size); // Clear buffer before receiving
    ssize_t bytes_received = recv(sock_fd, response_buf, response_buf_size - 1, 0); // -1 for null terminator
    if (bytes_received < 0) {
        perror("Client: recv failed");
        close(sock_fd);
        return -1;
    }
    // Null-terminate the received data to make it a valid C string
    response_buf[bytes_received] = '\0';

    // Close the socket
    close(sock_fd);
    return 0; // Success
}

// Implementation for parse_rpc_response using json-c: Parses server response.
int parse_rpc_response(const char *response_str, double *result_val, char *error_msg_buf, size_t error_msg_buf_size, int expected_id) {
    // Ensure null termination for safety, though response_str should be null-terminated by caller.
    if (response_str == NULL || result_val == NULL || error_msg_buf == NULL || error_msg_buf_size == 0) {
        if (error_msg_buf && error_msg_buf_size > 0) {
            strncpy(error_msg_buf, "Internal error: Null argument to parse_rpc_response", error_msg_buf_size - 1);
            error_msg_buf[error_msg_buf_size - 1] = '\0'; // Ensure null termination
        }
        return -1;
    }
    error_msg_buf[0] = '\0'; // Initialize error buffer to empty string

    struct json_object *root_obj = NULL;
    struct json_object *id_obj = NULL;
    struct json_object *result_obj = NULL;
    struct json_object *error_obj = NULL;      // For the "error" object itself
    struct json_object *error_msg_val_obj = NULL; // For the "message" field within "error"

    enum json_tokener_error jerr; // For detailed parsing error from json-c

    // Parse the entire JSON response string.
    // json_tokener_parse_verbose is preferred for better error reporting.
    root_obj = json_tokener_parse_verbose(response_str, &jerr);
    if (jerr != json_tokener_success || root_obj == NULL) {
        // Try to provide a more specific error if possible, otherwise use the generic description.
        // Note: json_tokener_get_parse_end might not be standard or available on all json-c versions.
        // A safer bet is just the error description.
        snprintf(error_msg_buf, error_msg_buf_size, "JSON parse error: %s", json_tokener_error_desc(jerr));
        return -1;
    }

    // Validate the "id" field in the response.
    if (!json_object_object_get_ex(root_obj, "id", &id_obj)) {
        strncpy(error_msg_buf, "Response JSON missing 'id' field", error_msg_buf_size - 1);
        error_msg_buf[error_msg_buf_size - 1] = '\0';
        json_object_put(root_obj); // Free the parsed root object
        return -1;
    }
    if (!json_object_is_type(id_obj, json_type_int)) {
        strncpy(error_msg_buf, "Response 'id' field is not an integer", error_msg_buf_size - 1);
        error_msg_buf[error_msg_buf_size - 1] = '\0';
        json_object_put(root_obj);
        return -1;
    }
    int response_id = json_object_get_int(id_obj);
    if (response_id != expected_id) {
        snprintf(error_msg_buf, error_msg_buf_size, "Response ID mismatch: expected %d, got %d", expected_id, response_id);
        json_object_put(root_obj);
        return -1;
    }

    // Check for the "error" field first. If present, it means the RPC call resulted in an error.
    if (json_object_object_get_ex(root_obj, "error", &error_obj)) {
        if (json_object_is_type(error_obj, json_type_object)) {
            // Try to get the "message" field from the "error" object.
            if (json_object_object_get_ex(error_obj, "message", &error_msg_val_obj)) {
                if (json_object_is_type(error_msg_val_obj, json_type_string)) {
                    strncpy(error_msg_buf, json_object_get_string(error_msg_val_obj), error_msg_buf_size - 1);
                    error_msg_buf[error_msg_buf_size - 1] = '\0';
                } else {
                    strncpy(error_msg_buf, "Error 'message' field is not a string", error_msg_buf_size - 1);
                    error_msg_buf[error_msg_buf_size - 1] = '\0';
                }
            } else {
                strncpy(error_msg_buf, "Error object in response is missing 'message' field", error_msg_buf_size - 1);
                error_msg_buf[error_msg_buf_size - 1] = '\0';
            }
        } else {
            strncpy(error_msg_buf, "Response 'error' field is not a JSON object", error_msg_buf_size - 1);
            error_msg_buf[error_msg_buf_size - 1] = '\0';
        }
        json_object_put(root_obj); // Free the root, which frees all its children
        return -1; // Error found and processed
    }

    // If no "error" field, look for the "result" field.
    if (json_object_object_get_ex(root_obj, "result", &result_obj)) {
        // For a calculator, result should be a number (double or int).
        if (json_object_is_type(result_obj, json_type_double) || json_object_is_type(result_obj, json_type_int)) {
            *result_val = json_object_get_double(result_obj); // json-c handles int to double conversion
            json_object_put(root_obj);
            return 0; // Success, result extracted
        } else {
            strncpy(error_msg_buf, "Response 'result' field is not a number", error_msg_buf_size - 1);
            error_msg_buf[error_msg_buf_size - 1] = '\0';
        }
    } else {
        // Neither "error" nor "result" field was found. This is an invalid JSON-RPC response.
        strncpy(error_msg_buf, "Invalid JSON-RPC response: missing 'result' or 'error' field", error_msg_buf_size - 1);
        error_msg_buf[error_msg_buf_size - 1] = '\0';
    }

    json_object_put(root_obj); // Free the root object in case of fallthrough errors
    return -1; // Fallthrough: indicates an issue like missing result/error or type mismatch for result
}


// RPC Wrapper Functions: Each constructs a specific JSON-RPC request, sends it, and processes the response.

// Calls the "add" method on the RPC server.
int rpc_add(double n1, double n2, double *result, char *error_buf, size_t error_buf_size) {
    char request_str[BUFFER_SIZE]; // Buffer for the JSON request string
    char response_buf[BUFFER_SIZE]; // Buffer for the server's response
    int current_id = request_id++; // Get unique ID for this request

    // Construct the JSON-RPC request string.
    sprintf(request_str, "{\"jsonrpc\": \"2.0\", \"method\": \"add\", \"params\": [%f, %f], \"id\": %d}", n1, n2, current_id);

    // Send the request and get the response.
    if (send_rpc_request(request_str, response_buf, BUFFER_SIZE) == 0) {
        // Parse the response.
        return parse_rpc_response(response_buf, result, error_buf, error_buf_size, current_id);
    } else {
        // send_rpc_request failed (e.g., connection error).
        strncpy(error_buf, "Failed to send/receive RPC request (network error)", error_buf_size - 1);
        error_buf[error_buf_size - 1] = '\0';
        return -1;
    }
}

// Calls the "subtract" method on the RPC server.
int rpc_subtract(double n1, double n2, double *result, char *error_buf, size_t error_buf_size) {
    char request_str[BUFFER_SIZE];
    char response_buf[BUFFER_SIZE];
    int current_id = request_id++;

    sprintf(request_str, "{\"jsonrpc\": \"2.0\", \"method\": \"subtract\", \"params\": [%f, %f], \"id\": %d}", n1, n2, current_id);

    if (send_rpc_request(request_str, response_buf, BUFFER_SIZE) == 0) {
        return parse_rpc_response(response_buf, result, error_buf, error_buf_size, current_id);
    } else {
        strncpy(error_buf, "Failed to send/receive RPC request (network error)", error_buf_size - 1);
        error_buf[error_buf_size - 1] = '\0';
        return -1;
    }
}

// Calls the "multiply" method on the RPC server.
int rpc_multiply(double n1, double n2, double *result, char *error_buf, size_t error_buf_size) {
    char request_str[BUFFER_SIZE];
    char response_buf[BUFFER_SIZE];
    int current_id = request_id++;

    sprintf(request_str, "{\"jsonrpc\": \"2.0\", \"method\": \"multiply\", \"params\": [%f, %f], \"id\": %d}", n1, n2, current_id);

    if (send_rpc_request(request_str, response_buf, BUFFER_SIZE) == 0) {
        return parse_rpc_response(response_buf, result, error_buf, error_buf_size, current_id);
    } else {
        strncpy(error_buf, "Failed to send/receive RPC request (network error)", error_buf_size - 1);
        error_buf[error_buf_size - 1] = '\0';
        return -1;
    }
}

// Calls the "divide" method on the RPC server.
int rpc_divide(double n1, double n2, double *result, char *error_buf, size_t error_buf_size) {
    char request_str[BUFFER_SIZE];
    char response_buf[BUFFER_SIZE];
    int current_id = request_id++;

    sprintf(request_str, "{\"jsonrpc\": \"2.0\", \"method\": \"divide\", \"params\": [%f, %f], \"id\": %d}", n1, n2, current_id);

    if (send_rpc_request(request_str, response_buf, BUFFER_SIZE) == 0) {
        return parse_rpc_response(response_buf, result, error_buf, error_buf_size, current_id);
    } else {
        strncpy(error_buf, "Failed to send/receive RPC request (network error)", error_buf_size - 1);
        error_buf[error_buf_size - 1] = '\0';
        return -1;
    }
}

// Note on compilation:
// This client uses the json-c library. To compile, you need to have libjson-c-dev (or equivalent) installed.
// Example compilation command:
// gcc client.c -o client -ljson-c
