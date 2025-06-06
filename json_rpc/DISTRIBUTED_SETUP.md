# Distributed Calculator Setup Guide

## 1. Overview

This document describes the setup and operation of a distributed calculator system. The system consists of the following main components:

-   **JSON-RPC Gateway (`json_rpc/server`):** The central entry point for clients. It receives JSON-RPC requests, routes them to appropriate backend calculation servers, and returns responses to the client. It also manages the lifecycle of configured backend processes and listens for their registrations.
-   **Backend Calculation Servers (e.g., `concurrent_tcp_async/server`, `iterative_udp/server`):** These are individual server processes that perform the actual calculations (add, subtract, multiply, divide). They can be TCP or UDP based. They register with the gateway upon startup.
-   **JSON-RPC Client (`json_rpc/client`):** A command-line utility to send JSON-RPC requests to the gateway.

The communication flow is as follows:
1.  The gateway starts and launches backend server processes as defined in `backends.conf`.
2.  Backend servers, upon starting, send a UDP registration message to the gateway, announcing their type (TCP/UDP), host, port, name, and supported operations.
3.  The client sends a JSON-RPC request (e.g., for "add") to the gateway.
4.  The gateway selects an appropriate registered backend server that supports the requested operation using a round-robin strategy.
5.  The gateway translates the JSON-RPC request into the simple protocol understood by the backend (e.g., "1 <num1> <num2>" for addition).
6.  The gateway forwards this translated request to the chosen backend server.
7.  The backend server processes the request and sends a simple response (e.g., "Result: <value>" or "Error: <message>").
8.  The gateway receives this response, translates it back into a JSON-RPC response format, and sends it to the client.

## 2. Prerequisites

Before compiling and running the system, ensure you have the following installed:

-   **`gcc` (GNU Compiler Collection):** For compiling the C source code.
-   **`make`:** For using the provided Makefiles to simplify compilation.
-   **`libjson-c-dev`:** The JSON-C library development files. This is specifically required for compiling `json_rpc/client.c` which uses this library for JSON parsing.
    -   On Debian/Ubuntu-based systems, you can install it using:
        ```bash
        sudo apt-get update
        sudo apt-get install gcc make libjson-c-dev
        ```

## 3. Compilation

Navigate to the root directory of the project structure.

### Compiling the Gateway Server

```bash
cd json_rpc
make server
# This will create an executable: json_rpc/server
cd ..
```

### Compiling Example Backend Servers

**Concurrent TCP Async Server:**

```bash
cd concurrent_tcp_async
make server
# This will create an executable: concurrent_tcp_async/server
cd ..
```

**Iterative UDP Server:**

```bash
cd iterative_udp
make server
# This will create an executable: iterative_udp/server
cd ..
```

Ensure the Makefiles in these directories are correctly set up to produce the executables with the specified names.

### Compiling the JSON-RPC Client

```bash
cd json_rpc
make client
# This will create an executable: json_rpc/client
# This step requires libjson-c-dev to be installed.
cd ..
```

## 4. Configuration (`json_rpc/backends.conf`)

The `json_rpc/backends.conf` file tells the gateway server which backend processes to launch and manage at startup.

-   **Purpose:** Defines a list of backend calculation servers that the gateway should start and monitor.
-   **Location:** Must be in the `json_rpc/` directory, named `backends.conf`.
-   **Format:** Each line defines one backend server with the following space-separated fields:
    `<executable_path> <server_name> <listen_host> <listen_port> <server_type>`

    -   `executable_path`: Relative or absolute path to the backend server's executable.
        -   *Note on relative paths:* Relative paths are typically interpreted relative to the directory where the `json_rpc/server` (gateway) is executed. It's generally recommended to use paths relative to the project root or ensure the gateway is run from the project root. For example, `../concurrent_tcp_async/server`.
    -   `server_name`: A unique name for this backend instance (e.g., `tcp_async_1`). This name is used in logs and for service registration.
    -   `listen_host`: The IP address the backend server should listen on (e.g., `127.0.0.1`). This is also the host that the backend will report during its registration to the gateway.
    -   `listen_port`: The port number the backend server should listen on (e.g., `9001`). This is also the port the backend will report during registration.
    -   `server_type`: The protocol type of the backend server. Must be either `TCP` or `UDP`.

-   **Example `backends.conf` content:**

    ```
    ../concurrent_tcp_async/server tcp_async_1 127.0.0.1 9001 TCP
    ../iterative_udp/server udp_iter_1 127.0.0.1 9002 UDP
    ../concurrent_tcp_async/server tcp_async_2 127.0.0.1 9003 TCP
    ../iterative_udp/server udp_iter_2 127.0.0.1 9004 UDP
    ```

    -   In this example:
        -   The first line launches `concurrent_tcp_async/server`, names it `tcp_async_1`, configures it to listen on `127.0.0.1:9001`, and identifies it as a `TCP` server.
        -   The second line launches `iterative_udp/server`, names it `udp_iter_1`, configures it for `127.0.0.1:9002` as a `UDP` server.

-   **Important:** The backend servers themselves must be compiled and present at the specified `executable_path` for the gateway to launch them successfully.

## 5. Running the System

Ensure all components (gateway, backends, client) are compiled as described in Section 3.

### Step 1: Configure `backends.conf`

-   Open `json_rpc/backends.conf` in a text editor.
-   Verify that the `executable_path` for each backend server correctly points to its compiled executable relative to where you will run the `json_rpc/server`.
-   Ensure `server_name` is unique for each entry.
-   Choose appropriate `listen_host` and `listen_port` for each backend. These ports must be available.

### Step 2: Run the JSON-RPC Gateway Server

It's generally best to run the gateway from the project's root directory to ensure relative paths in `backends.conf` work as expected.

```bash
./json_rpc/server
```

**What to look for in the gateway logs:**

-   **Gateway Listening Port:**
    `[YYYY-MM-DD HH:MM:SS] [INFO] JSON-RPC Server listening on port 8080` (or your `DEFAULT_PORT`)
-   **Discovery Port Listening:**
    `[YYYY-MM-DD HH:MM:SS] [INFO] Discovery UDP socket listening on 0.0.0.0:8081` (or your `GATEWAY_DISCOVERY_HOST`:`GATEWAY_DISCOVERY_PORT`)
-   **Backend Launch Attempts (from `backends.conf`):**
    `[YYYY-MM-DD HH:MM:SS] [INFO] Attempting to launch backend: ../concurrent_tcp_async/server (Name: tcp_async_1, Host: 127.0.0.1, Port: 9001, Type: TCP)`
    `[YYYY-MM-DD HH:MM:SS] [INFO] Backend tcp_async_1 launched successfully with PID: <pid>`
-   **Backend Registration Messages (from launched or independently started backends):**
    `[YYYY-MM-DD HH:MM:SS] [INFO] Received registration message from 127.0.0.1:<backend_udp_port> : type=TCP;host=127.0.0.1;port=9001;name=tcp_async_1;ops=add,subtract,multiply,divide`
    `[YYYY-MM-DD HH:MM:SS] [INFO] Registered new backend: tcp_async_1 (Type: TCP, Host: 127.0.0.1, Port: 9001, Ops: add,subtract,multiply,divide)`
    or
    `[YYYY-MM-DD HH:MM:SS] [INFO] Updated registration for backend: tcp_async_1 (...)`

If `backends.conf` is empty or all launch attempts fail, you might see:
`[YYYY-MM-DD HH:MM:SS] [WARNING] CRITICAL SETUP: No backends were successfully launched from backends.conf...`

### Step 3: Run the JSON-RPC Client

Open a new terminal. The client sends a JSON-RPC request to the gateway.

**Example Client Usage:**

To request an "add" operation with parameters 10 and 5:

```bash
./json_rpc/client '{"jsonrpc": "2.0", "method": "add", "params": [10.0, 5.0], "id": 1}'
```

To request a "divide" operation:

```bash
./json_rpc/client '{"jsonrpc": "2.0", "method": "divide", "params": [100.0, 10.0], "id": 2}'
```

**Observing Gateway Logs for Routing:**

When the client sends a request, check the gateway's terminal output. You should see logs indicating how the request is processed:

-   `[YYYY-MM-DD HH:MM:SS] [DEBUG] Received JSON-RPC request: {"jsonrpc": "2.0", ...}`
-   `[YYYY-MM-DD HH:MM:SS] [INFO] Selected backend <backend_name> for operation <method_name> via round-robin...`
-   `[YYYY-MM-DD HH:MM:SS] [INFO] Routing request for method '<method_name>' (id: <id>) to backend: <backend_name> (<host>:<port>)`
-   `[YYYY-MM-DD HH:MM:SS] [INFO] Attempting <TCP/UDP> communication with <backend_name> ... Payload: "<op_code> <param1> <param2>"`
-   `[YYYY-MM-DD HH:MM:SS] [INFO] <TCP/UDP> received from backend <backend_name>: Result: <value>`
-   `[YYYY-MM-DD HH:MM:SS] [DEBUG] Sending JSON-RPC response (id: <id>): {"jsonrpc": "2.0", "result": <value>, "id": <id>}`

The client will print the JSON-RPC response it receives from the gateway.

## 6. Key Mechanisms Explained

-   **Process Management:**
    The gateway server (`json_rpc/server`) is responsible for launching and monitoring backend calculation servers as defined in `json_rpc/backends.conf`. If a managed backend process exits unexpectedly, the gateway will attempt to relaunch it. This provides a basic level of fault tolerance.

-   **Service Registration:**
    -   When a backend server (either launched by the gateway or started independently) starts up, it sends a UDP registration message to the gateway's discovery port (`GATEWAY_DISCOVERY_PORT`, typically 8081).
    -   **Message Format:** The registration message is a plain text string with key-value pairs separated by semicolons (`;`), and keys and values separated by equals signs (`=`).
        Example: `type=TCP;host=127.0.0.1;port=9001;name=tcp_async_1;ops=add,subtract,multiply,divide`
        -   `type`: `TCP` or `UDP`.
        -   `host`: IP address of the backend.
        -   `port`: Port number of the backend.
        -   `name`: Unique name of the backend instance.
        -   `ops`: Comma-separated list of operations supported (e.g., `add,subtract,multiply,divide`).
    -   The gateway maintains a list of these registered backends, updating their `last_seen` time and `is_active` status. (Note: The current implementation always sets `is_active=1` on registration/update; a timeout mechanism to mark inactive backends is a potential future enhancement).

-   **Dynamic Routing:**
    -   When the gateway receives a JSON-RPC request, it determines the `method` (e.g., "add").
    -   It then consults its list of currently registered and active backend servers.
    -   It filters this list to find backends that support the requested operation (based on the `ops` field in their registration).
    -   If multiple suitable backends are found, the gateway uses a simple **round-robin** strategy to select one. This helps distribute the load among available backends.
    -   If no suitable backend is found, an error is returned to the client.

-   **Protocol Translation:**
    -   **Client to Gateway:** The client communicates with the gateway using JSON-RPC 2.0 over TCP.
    -   **Gateway to Backend:** The backend servers expect a simpler protocol:
        -   A plain text string: `<op_code> <param1> <param2>`
        -   `op_code`: An integer (1 for add, 2 for subtract, 3 for multiply, 4 for divide).
        -   `param1`, `param2`: Floating-point numbers.
    -   The gateway translates the incoming JSON-RPC `method` and `params` into this simpler format before forwarding to the backend.
    -   **Backend to Gateway:** Backends respond with:
        -   `Result: <value>` for success.
        -   `Error: <error_message>` for failure.
    -   The gateway parses this simple text response and translates it back into a valid JSON-RPC response (either a `result` or an `error` object) for the client.

## 7. Troubleshooting Tips

-   **Gateway Server Logs (`json_rpc/server` output):** This is the most important place to look.
    -   Check for successful binding to the JSON-RPC port and the UDP discovery port.
    -   Verify that backends listed in `backends.conf` are being launched (or if there are errors during launch like "execv failed").
    -   Look for "Received registration message" and "Registered new backend" (or "Updated registration") messages. If these are missing, your backends are not successfully registering.
    -   When a client request comes in, observe the "Selected backend" and "Routing request" messages to see where the gateway is attempting to send the work.
    -   Check for communication errors with backends (e.g., "Failed to connect", "Timeout receiving data").

-   **Backend Server Logs (e.g., `concurrent_tcp_async/server` output):**
    -   Ensure the backend server starts and binds to the host/port specified in `backends.conf`.
    -   Look for logs indicating it's attempting to register with the gateway (e.g., "Registration message sent to <gateway_host>:<gateway_port>").
    -   Check for any errors if it receives a request from the gateway.

-   **Verify `backends.conf` Paths:**
    -   Double-check that the `executable_path` for each backend in `json_rpc/backends.conf` is correct *relative to the directory where you are running the `json_rpc/server` executable*.
    -   If using relative paths like `../concurrent_tcp_async/server`, it's usually best to run `json_rpc/server` from the project's root directory.

-   **Check if Backend Processes Are Running:**
    -   After starting `json_rpc/server`, use commands like `ps aux | grep server` (or more specific names) to see if the backend processes (e.g., `concurrent_tcp_async/server`) were actually launched and are still running.
    -   If they are not running, check the gateway logs for `fork` or `execv` errors related to that backend.

-   **Firewall Issues:**
    -   Ensure no firewalls are blocking TCP connections to the gateway's JSON-RPC port (default 8080) from the client.
    -   Ensure no firewalls are blocking UDP packets to the gateway's discovery port (default 8081) from the backend servers.
    -   Ensure no firewalls are blocking TCP/UDP connections from the gateway machine to the backend server machines on their respective ports.

-   **Client Errors:**
    -   If the client gets an error, correlate it with the gateway logs. The gateway might provide more details about why the request failed (e.g., "Method not supported", "Error communicating with backend").

-   **Supported Operations:**
    -   Ensure the `ops=` field in the backend's registration message (and thus how the backend is programmed) correctly lists all operations it supports. The gateway uses this for routing. The standard backends support `add,subtract,multiply,divide`.

This guide should help in setting up, running, and troubleshooting the distributed calculator system.
