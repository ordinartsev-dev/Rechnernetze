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

DynamicResource dynamic_resources[100];
int resource_count = 0;

// Forward declaration
int check(char *str);

static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
    };
    struct addrinfo *result_info;
    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
        fprintf(stderr, "Error parsing host/port");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in result = *((struct sockaddr_in*) result_info->ai_addr);
    freeaddrinfo(result_info);
    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <IP-address> <port>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *port = argv[2];
    int port_number = atoi(port);
    if (port_number <= 0 || port_number > 65535) {
        printf("Invalid port number. Please provide a value between 1 and 65535.\n");
        return 1;
    }

    struct sockaddr_in server_addr = derive_sockaddr(host, port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Socket creation failed: %s\n", strerror(errno));
        return 1;
    }

    printf("Socket created\n");

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Failed to set SO_REUSEADDR");
        close(sock);
        return 1;
    }

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        printf("Bind failed: %s\n", strerror(errno));
        close(sock);
        return 1;
    }

    printf("Bind successful on %s:%s\n", host, port);

    if (listen(sock, 10) == -1) {
        printf("Listen failed: %s\n", strerror(errno));
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %s\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == -1) {
            perror("Accept failed");
            close(sock);
            return 1;
        }
        printf("Accepted connection\n");

        char request_buffer[8192];
        int buffer_length = 0;
        memset(request_buffer, 0, sizeof(request_buffer));

        // Keep reading until client closes or error occurs
        while (1) {
            char buffer[8192];
            ssize_t received = recv(client_sock, buffer, sizeof(buffer), 0);

            if (received == -1) {
                perror("Receive failed");
                break;
            }

            if (received == 0) {
                // Client closed connection
                perror("Connection was closed by the client");
                break;
            }

            if (buffer_length + (int)received > (int)sizeof(request_buffer)) {
                fprintf(stderr, "Error: Request too large\n");
                break;
            }
            memcpy(request_buffer + buffer_length, buffer, received);
            buffer_length += received;

            // Process all complete requests in request_buffer
            int processed = 1;
            while (processed) {
                processed = 0;
                char *header_end = strstr(request_buffer, "\r\n\r\n");
                if (header_end) {
                    // Temporarily end headers to parse them
                    *header_end = '\0';
                    char *body_start = header_end + 4;

                    // Find Content-Length
                    int content_length = 0;
                    {
                        char headers_copy[8192];
                        strncpy(headers_copy, request_buffer, sizeof(headers_copy)-1);
                        headers_copy[sizeof(headers_copy)-1] = '\0';
                        char *line = strtok(headers_copy, "\r\n");
                        // first line is request line
                        line = strtok(NULL, "\r\n");
                        while (line) {
                            char *delim = strchr(line, ':');
                            if (delim) {
                                *delim = '\0';
                                char *key = line;
                                char *value = delim + 1;
                                while (*value == ' ') value++;
                                if (strcasecmp(key, "Content-Length") == 0) {
                                    content_length = atoi(value);
                                    break;
                                }
                            }
                            line = strtok(NULL, "\r\n");
                        }
                    }

                    int header_size = (int)(body_start - request_buffer);
                    if (header_size + content_length > buffer_length) {
                        // Not enough data for body
                        // Restore and wait for more data
                        *header_end = '\r';
                        continue;
                    }

                    // We have a full request
                    memset(global_body, 0, sizeof(global_body));
                    if (content_length > 0) {
                        memcpy(global_body, body_start, content_length);
                        global_body[content_length] = '\0';
                    }

                    // Now we can safely call check
                    int status_code = check(request_buffer);

                    // Create response
                    char response[16384];
                    memset(response, 0, sizeof(response));
                    snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");

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
                            if (strcmp(resource_name, "foo") == 0) {
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nFoo");
                            } else if (strcmp(resource_name, "bar") == 0) {
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nBar");
                            } else if (strcmp(resource_name, "baz") == 0) {
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nBaz");
                            } else {
                                snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                            }
                        } else if (strncmp(last_uri, "/dynamic/", 9) == 0) {
                            char *resource_name = last_uri + 9;
                            int found = 0;
                            for (int i = 0; i < resource_count; i++) {
                                if (strcmp(dynamic_resources[i].path, resource_name) == 0) {
                                    snprintf(response, sizeof(response),
                                             "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n%s",
                                             strlen(dynamic_resources[i].content),
                                             dynamic_resources[i].content);
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found) {
                                snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                            }
                        }
                    } else if (status_code == 201) {
                        snprintf(response, sizeof(response), "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
                    } else if (status_code == 403) {
                        snprintf(response, sizeof(response), "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
                    } else if (status_code == 204) {
                        snprintf(response, sizeof(response), "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
                    }

                    int bytes_sent = send(client_sock, response, strlen(response), 0);
                    if (bytes_sent == -1) {
                        perror("Send failed");
                        break; // break from connection loop
                    }

                    // Clear global arrays for next request
                    memset(global_body, 0, sizeof(global_body));
                    memset(last_uri, 0, sizeof(last_uri));

                    // Move leftover data to front of request_buffer
                    int total_request_size = header_size + content_length;
                    int leftover = buffer_length - total_request_size;
                    if (leftover > 0) {
                        memmove(request_buffer, request_buffer + total_request_size, leftover);
                    }
                    buffer_length = leftover;

                    processed = 1;
                }
            }
        }

        // Close the client socket
        close(client_sock);
    }

    close(sock);
    return 0;
}

int check(char *str) {
    if (str == NULL) {
        fprintf(stderr, "Error: NULL string passed\n");
        return -1;
    }

    char method[16], version[16];
    int content_length = 0;

    // Extract the start line
    char *line = strtok(str, "\r\n");
    if (line == NULL || sscanf(line, "%15s %255s %15s", method, last_uri, version) != 3) {
        fprintf(stderr, "Error: Invalid request line\n");
        return 400;
    }

    int header_count = 0;
    char *header = strtok(NULL, "\r\n");
    while (header) {
        if (header_count >= 40) {
            fprintf(stderr, "Error: Too many headers\n");
            return 400;
        }
        header_count++;

        char *delimiter = strchr(header, ':');
        if (!delimiter) {
            fprintf(stderr, "Error: Invalid header format: %s\n", header);
            return 400;
        }

        *delimiter = '\0';
        char *key = header;
        char *value = delimiter + 1;
        while (*value == ' ') value++;

        if (strlen(key) > 256) {
            fprintf(stderr, "Error: Header name too long: %s\n", key);
            return 400;
        }
        if (strlen(value) > 256) {
            fprintf(stderr, "Error: Header value too long: %s\n", value);
            return 400;
        }

        if (strcasecmp(key, "Content-Length") == 0) {
            content_length = atoi(value);
            if (content_length > 8192) {
                fprintf(stderr, "Error: Content-Length too large\n");
                return 413; // Payload Too Large
            }
            printf("Parsed Content-Length: %d\n", content_length);
        }

        header = strtok(NULL, "\r\n");
    }

    if (strcmp(method, "GET") == 0) {
        if (strncmp(last_uri, "/static/", 8) == 0) {
            char *resource_name = last_uri + 8;
            if (strcmp(resource_name, "foo") == 0 ||
                strcmp(resource_name, "bar") == 0 ||
                strcmp(resource_name, "baz") == 0) {
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
            for (int i = 0; i < resource_count; i++) {
                if (strcmp(dynamic_resources[i].path, resource_name) == 0) {
                    strncpy(dynamic_resources[i].content, global_body, 8191);
                    dynamic_resources[i].content[8191] = '\0';
                    return 204; // No Content
                }
            }
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

    } else if (strcmp(method, "DELETE") == 0) {
        if (strncmp(last_uri, "/dynamic/", 9) == 0) {
            char *resource_name = last_uri + 9;
            for (int i = 0; i < resource_count; i++) {
                if (strcmp(dynamic_resources[i].path, resource_name) == 0) {
                    for (int j = i; j < resource_count - 1; j++) {
                        dynamic_resources[j] = dynamic_resources[j + 1];
                    }
                    resource_count--;
                    return 204; // No Content
                }
            }
            return 404; // Resource not found
        } else {
            return 403;
        }
    } else {
        // For HEAD or other methods not implemented
        fprintf(stderr, "Error: Method not implemented: %s\n", method);
        return 501; // Not implemented
    }

    return 0;
}
