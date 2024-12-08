#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>


static char last_uri[256] = {0};
char global_body[8192] = {0};

typedef struct {
    char path[256];
    char content[8192];
} DynamicResource;

DynamicResource dynamic_resources[100]; // Resource array
int resource_count = 0;                 // Resource counter

int get_content_length(const char *buffer) {
    if (buffer == NULL) {
        fprintf(stderr, "Error: NULL string passed\n");
        return -1; // Indicates error
    }

    int content_length = 0;

    // Use a copy of the buffer for tokenization since `strtok` modifies the string
    char *buffer_copy = strdup(buffer);
    if (!buffer_copy) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return -1;
    }

    // Extract the start line
    char *line = strtok(buffer_copy, "\r\n");
    if (line == NULL) {
        fprintf(stderr, "Error: Invalid request format\n");
        free(buffer_copy);
        return -1;
    }

    // Process headers
    int header_count = 0;
    char *header = strtok(NULL, "\r\n");
    while (header) {
        if (header_count >= 40) {
            fprintf(stderr, "Error: Too many headers\n");
            free(buffer_copy);
            return -1;
        }
        header_count++;

        // Find the ':' delimiter
        char *delimiter = strchr(header, ':');
        if (!delimiter) {
            fprintf(stderr, "Error: Invalid header format: %s\n", header);
            free(buffer_copy);
            return -1;
        }

        // Split the header key and value
        *delimiter = '\0';
        char *key = header;
        char *value = delimiter + 1;

        // Trim leading spaces from the value
        while (isspace((unsigned char)*value)) value++;

        // Validate header lengths
        if (strlen(key) > 256 || strlen(value) > 256) {
            fprintf(stderr, "Error: Header too long: %s\n", key);
            free(buffer_copy);
            return -1;
        }

        // Check for Content-Length
        if (strcasecmp(key, "Content-Length") == 0) {
            content_length = atoi(value);
            if (content_length > 8192) {
                fprintf(stderr, "Error: Content-Length too large\n");
                free(buffer_copy);
                return -1;
            }
            break; // No need to parse further headers
        }

        // Proceed to the next header
        header = strtok(NULL, "\r\n");
    }

    free(buffer_copy);
    return content_length;
}

int check(char *str) {
    if (str == NULL) {
        fprintf(stderr, "Error: NULL string passed\n");
        return -1;
    }

    // Variables for the start line
    char method[16], version[16];
    int content_length = 0;

    // Extract the start line
    char *line = strtok(str, "\r\n");
    if (line == NULL || sscanf(line, "%15s %255s %15s", method, last_uri, version) != 3) {
        // Incorrect start line
        fprintf(stderr, "Error: Invalid request line\n");
        return 400;
    }

    // Check headers
    int header_count = 0;  // Counter for headers
    char *header = strtok(NULL, "\r\n");
    while (header) {
        if (header_count >= 40) {
            fprintf(stderr, "Error: Too many headers\n");
            return 400;
        }
        header_count++;

        // Check the separator ':'
        char *delimiter = strchr(header, ':');
        if (!delimiter) {
            fprintf(stderr, "Error: Invalid header format: %s\n", header);
            return 400;
        }

        // Divide the key's and header's values
        *delimiter = '\0'; // Replace ':' with '\0'
        char *key = header;
        char *value = delimiter + 1;

        // Remove leading spaces from the value
        while (*value == ' ') value++;

        // Check length of key and value
        if (strlen(key) > 256) {
            fprintf(stderr, "Error: Header name too long: %s\n", key);
            return 400;
        }

        if (strlen(value) > 256) {
            fprintf(stderr, "Error: Header value too long: %s\n", value);
            return 400;
        }

        // Process the header
        if (strcasecmp(key, "Content-Length") == 0) {
            content_length = atoi(value);
            if (content_length > 8192) {
                fprintf(stderr, "Error: Content-Length too large\n");
                return 413; // Payload Too Large
            }
            printf("Parsed Content-Length: %d\n", content_length);
        }

        // Proceed to the next header
        header = strtok(NULL, "\r\n");
    }

    // Check the methods
    // Processing method GET
    if (strcmp(method, "GET") == 0) {
        // Check the existence of the file
        if (strncmp(last_uri, "/static/", 8) == 0) {
            char *resource_name = last_uri + 8;
            if (strcmp(resource_name, "foo") == 0) {
                // "foo" is found
                return 200;
            } else if (strcmp(resource_name, "bar") == 0) {
                // "bar" is found
                return 200;
            } else if (strcmp(resource_name, "baz") == 0) {
                // "baz" is found
                return 200;
            } else {
                return 404;
            }
        } else if (strncmp(last_uri, "/dynamic/", 9) == 0) {
            char *resource_name = last_uri + 9;
            for (int i = 0; i < resource_count; i++) {
                if (strcmp(dynamic_resources[i].path, resource_name) == 0) {
                    return 200;
                }
            }
            return 404;
        } else {
            return 404;
        }

    } else if (strcmp(method, "PUT") == 0) {
        if (strncmp(last_uri, "/dynamic/", 9) == 0) {
            char *resource_name = last_uri + 9;
            // Check if resource exists
            for (int i = 0; i < resource_count; i++) {
                if (strcmp(dynamic_resources[i].path, resource_name) == 0) {
                    // Update existing resource
                    strncpy(dynamic_resources[i].content, global_body, 8191);
                    dynamic_resources[i].content[8191] = '\0';
                    return 204; // No Content
                }
            }

            // Create new resource if not found
            if (resource_count < 100) {
                strncpy(dynamic_resources[resource_count].path, resource_name, 255);
                dynamic_resources[resource_count].path[255] = '\0';
                strncpy(dynamic_resources[resource_count].content, global_body, 8191);
                dynamic_resources[resource_count].content[8191] = '\0';
                resource_count++;
                return 201; // Created
            }
            return 403; // Forbidden
        }
        return 403; // Forbidden
    }
    // Process DELETE
    else if (strcmp(method, "DELETE") == 0) {
        if (strncmp(last_uri, "/dynamic/", 9) == 0) {
            char *resource_name = last_uri + 9;
            for (int i = 0; i < resource_count; i++) {
                if (strcmp(dynamic_resources[i].path, resource_name) == 0) {
                    for (int j = i; j < resource_count - 1; j++) {
                        dynamic_resources[j] = dynamic_resources[j + 1];
                    }
                    resource_count--;
                    return 204; // Resource deleted successfully
                }
            }
            return 404; // Resource not found
        } else {
            return 403;
        }
    } else {
        fprintf(stderr, "Error: Method not implemented: %s\n", method);
        return 501; // This method is not implemented
    }

    return 0; // Success
}

static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
     };
    struct addrinfo *result_info;
    // Resolve the host (IP address or hostname) into a list of possible addresses.
    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
        fprintf(stderr, "Error␣parsing␣host/port");
        exit(EXIT_FAILURE);
    }
    // Copy the sockaddr_in structure from the first address in the list
    struct sockaddr_in result = *((struct sockaddr_in*) result_info->ai_addr);
    // Free the allocated memory for the result_info
    freeaddrinfo(result_info);
    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 3) { // Expected two arguments: IP-address and port-number
        printf("Usage: %s <IP-address> <port>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *port = argv[2];

    // Extract integer out of the char
    int port_number = atoi(port);
    if (port_number <= 0 || port_number > 65535)    // Check that the port-number is correct
    {
        printf("Invalid port number. Please provide a value between 1 and 65535.\n");
        return 1;
    }

    struct sockaddr_in server_addr = derive_sockaddr(host, port);

    // Now server_addr contains IP and port-number for usage in bind() or connect()

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    // Check if the creation of socket is not successful
    if (sock == -1)
    {
        printf("Socket creation failed with error: %s\n", strerror(errno));
        return 1;
    }

    printf("Socket created\n");

    // Installing SO_REUSEADDR for socket
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Failed to set SO_REUSEADDR");
        close(sock);
        return 1;
    }

    // Bind the socket to the port and address
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        printf("Bind failed: %s\n", strerror(errno));
        close(sock);    // Close socket
        return 1;
    }

    printf("Bind successful on %s:%s\n", host, port);

    // Switch the socket into listen mode
    if (listen(sock, 10) == -1) // Queue up to 10 connections
    {
        printf("Listen failed: %s\n", strerror(errno));
        close(sock);    // Close the socket
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %s\n", port);

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == -1)
        {
            perror("Accept failed");
            close(sock);
            return 1;
        }
        printf("Accepted connection\n");

        while (1)
        {
            char buffer[8192];
            static char request_buffer[8192] = {0}; // Adjusted buffer size to 8192 bytes
            static int buffer_length = 0;          // Current length of data in buffer

            ssize_t received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

            if (received == -1)
            {
                perror("Receive failed");
                break;
            }

            if (received == 0)
            {
                perror("Connection was closed by the client");
                break;
            }

            if (buffer_length + received >= sizeof(request_buffer)) {
                fprintf(stderr, "Error: Request too large\n");
                break;
            }

            // Copy received data into request_buffer
            memcpy(request_buffer + buffer_length, buffer, received);
            buffer_length += received;
            request_buffer[buffer_length] = '\0';

            if (received > 0)
            {
                printf("Received data: %s\n", buffer);

                const char *word = "\r\n\r\n";
                char *result = strstr(request_buffer, word);
                
                int payload_len = -1; 
                int content_length = 0;

                if (result != NULL)
                {
                    content_length = get_content_length(request_buffer);
                    payload_len = (result - request_buffer) + strlen(word);
                }
                

                if (payload_len >= content_length)
                {
                    *result = '\0';

                    char *body = result + 4; // Pointer at the request-body (if it exists)

                    // Copy the request body into global_body
                    strncpy(global_body, body, sizeof(global_body) - 1);
                    global_body[sizeof(global_body) - 1] = '\0';

                    int status_code = check(request_buffer);

                    // Creating the response
                    char response[16384]; // Adjusted response buffer size
                    if (status_code == 400) {
                        snprintf(response, sizeof(response), "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
                    } else if (status_code == 404) {
                        snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                    } else if (status_code == 413) {
                        snprintf(response, sizeof(response), "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n");
                    } else if (status_code == 501) {
                        snprintf(response, sizeof(response), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n");
                    } else if (status_code == 200) {
                        if (strncmp(last_uri, "/static/", 8) == 0) {
                            char *resource_name = last_uri + 8;

                            // Creating response-body in relation to the resource
                            if (strcmp(resource_name, "foo") == 0) {
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nFoo");
                            } else if (strcmp(resource_name, "bar") == 0) {
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nBar");
                            } else if (strcmp(resource_name, "baz") == 0) {
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nBaz");
                            } else {
                                snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                            }
                        }
                        else if (strncmp(last_uri, "/dynamic/", 9) == 0)
                        {
                            // Form the dynamic response with dynamic content
                            char *resource_name = last_uri + 9;
                            for (int i = 0; i < resource_count; i++)
                            {
                                if (strcmp(dynamic_resources[i].path, resource_name) == 0)
                                {
                                    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n%s",
                                                strlen(dynamic_resources[i].content), dynamic_resources[i].content);
                                    break;
                                }
                            }
                        }
                    }
                    else if (status_code == 201) {
                        snprintf(response, sizeof(response), "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
                    }
                    else if (status_code == 403) {
                        snprintf(response, sizeof(response), "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
                    }
                    else if (status_code == 204) {
                        snprintf(response, sizeof(response), "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
                    }
                    else {
                        snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
                    }
                    // Send the response
                    int bytes_sent = send(client_sock, response, strlen(response), 0);

                    if (bytes_sent == -1)
                    {
                        perror("Send failed");  // Send failed!
                    }

                    // Clear the buffer after it has been processed
                    memset(request_buffer, 0, sizeof(request_buffer));
                    buffer_length = 0;
                    memset(global_body, 0, sizeof(global_body));
                }

            }
        }

        // Close the client socket
        close(client_sock);
    }
    // Close the socket
    close(sock);
    return 0;
}
