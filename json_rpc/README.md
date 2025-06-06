# JSON-RPC Calculator

This JSON-RPC calculator is a new, standalone implementation based on the core calculation logic originally found in `calculator/monolithic.c`. It operates independently of other calculator examples within this repository (such as the iterative or concurrent raw socket-based versions).

This directory contains a simple calculator application refactored to use a client-server architecture based on JSON-RPC 2.0 over TCP.

## RPC Framework Choice: JSON-RPC

JSON-RPC was chosen for this implementation due to its:
- **Simplicity:** It's a lightweight, text-based protocol that is relatively easy to implement and debug.
- **Readability:** JSON is human-readable, making it easier to inspect requests and responses during development.
- **Wide Support (Conceptually):** While this implementation is custom for C, JSON is a widely understood format, and JSON-RPC has libraries in many languages if extending the system.
- **No Complex Dependencies for Server:** The server-side JSON parsing is implemented manually for basic needs, avoiding heavy external library requirements for the server. The client uses `libjson-c` for more robust response parsing.

## JSON-RPC Message Formats

The communication follows the JSON-RPC 2.0 specification.

### Request Format

All requests are JSON objects with the following structure:
```json
{
  "jsonrpc": "2.0",
  "method": "<method_name>",
  "params": [<param1>, <param2>],
  "id": <request_id>
}
```
- `method_name`: Can be "add", "subtract", "multiply", or "divide".
- `params`: An array of two numbers.
- `request_id`: An integer identifier for the request.

**Example Request (add):**
```json
{
  "jsonrpc": "2.0",
  "method": "add",
  "params": [10, 5],
  "id": 1
}
```

### Response Format

**Successful Response:**
```json
{
  "jsonrpc": "2.0",
  "result": <calculation_result>,
  "id": <request_id>
}
```

**Error Response:**
```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": <error_code>,
    "message": "<error_message>"
  },
  "id": <request_id>
}
```
- Example `error_code` for server-defined errors: -32000.

**Supported Operations:**

1.  **`add`**
    *   `params`: `[<number1>, <number2>]`
    *   `result`: Sum of the numbers.
2.  **`subtract`**
    *   `params`: `[<minuend>, <subtrahend>]`
    *   `result`: Difference of the numbers.
3.  **`multiply`**
    *   `params`: `[<number1>, <number2>]`
    *   `result`: Product of the numbers.
4.  **`divide`**
    *   `params`: `[<dividend>, <divisor>]`
    *   `result`: Quotient.
    *   `error`: If `divisor` is 0, an error object with message "Division by zero".

## Compilation

A `Makefile` is provided to compile the server and client.

**Prerequisites:**
- A C compiler (e.g., GCC).
- `make` utility.
- **`libjson-c-dev`**: The client program uses the `json-c` library for parsing JSON responses. Install it on Debian/Ubuntu based systems using:
  ```bash
  sudo apt-get update
  sudo apt-get install libjson-c-dev
  ```
  For other systems, install the equivalent `json-c` development package. The Makefile assumes headers are in `/usr/include/json-c`. If they are installed elsewhere, you might need to adjust the `CFLAGS` in the `Makefile`.

  **Note on `json-c` headers:** The `client.c` program uses the general `<json-c/json.h>` header. If you encounter compilation errors related to missing `json-c` types (like `json_type`) or functions (like `json_tokener_error_desc`), it might indicate that your `json-c` installation has a different header structure, or the main `json.h` doesn't automatically include all necessary sub-definitions. In such cases, ensure your `libjson-c-dev` (or equivalent package) is correctly and completely installed. Double-check that the include path in the `CFLAGS` variable within the `Makefile` (e.g., `-I/usr/include/json-c`) accurately points to the root directory of your system's `json-c` header files. Some systems might place these headers in slightly different locations like `/usr/include/json` or within version-specific subdirectories.

**Compile:**
Navigate to the `calculator/json_rpc/` directory and run:
```bash
make all
```
This will create two executables: `server` and `client`.

## Running the Application

1.  **Start the Server:**
    Open a terminal, navigate to `calculator/json_rpc/`, and run:
    ```bash
    ./server
    ```
    The server will start and listen on port 8080 by default. You should see a message like: `Server listening on port 8080...`

2.  **Run the Client:**
    Open another terminal, navigate to `calculator/json_rpc/`, and run:
    ```bash
    ./client
    ```
    The client will prompt you to enter an operation (add, subtract, multiply, divide) and two numbers.

    **Example Client Interaction:**
    ```
    Enter operation (add, sub, mul, div) or 'exit': add
    Enter first number: 10
    Enter second number: 5
    Result: 15.00
    ```
    ```
    Enter operation (add, sub, mul, div) or 'exit': divide
    Enter first number: 10
    Enter second number: 0
    Error: Division by zero
    ```
    Enter `exit` to quit the client.

## Error Handling

- The server handles basic errors like invalid JSON requests, unknown methods, and division by zero, returning appropriate JSON-RPC error responses.
- The client handles network connection errors and displays error messages received from the server.
