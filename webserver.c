#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

static char last_uri[256] = {0};
char global_body[8192] = {0};

typedef struct {
    char path[256];
    char content[8192];
} DynamicResource;

DynamicResource dynamic_resources[100]; // Resource array
int resource_count = 0;                 // Resource counter

void log_dynamic_resources_to_file(const char *filename) {
    FILE *file = fopen(filename, "w"); // Open the file for writing
    if (file == NULL) {
        perror("Failed to open file for logging");
        return;
    }

    fprintf(file, "Current dynamic resources:\n");
    for (int i = 0; i < resource_count; i++) {
        fprintf(file, "Resource %d:\n", i + 1);
        fprintf(file, "  Path: %s\n", dynamic_resources[i].path);
        fprintf(file, "  Content: %s\n", dynamic_resources[i].content);
    }
    if (resource_count == 0) {
        fprintf(file, "No dynamic resources found.\n");
    }

    fclose(file); // Close the file
}

int check(char *str) {
    if (str == NULL) {
        fprintf(stderr, "Error: NULL string passed\n");
        return -1;
    }

    char method[16], version[16];
    int content_length = 0;

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

    int received_body_length = (int)strlen(global_body);
    if (content_length > 0 && received_body_length < content_length) {
        // Incomplete payload
        return -2;
    }

    // If we reach here, we have the full payload or no payload needed
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
                    return 204;
                }
            }
            return 404;
        } else {
            return 403;
        }
    } else {
        fprintf(stderr, "Error: Method not implemented: %s\n", method);
        return 501;
    }

    return 0;
}

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

int main(int argc, char *argv[]) {
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
        printf("Socket creation failed with error: %s\n", strerror(errno));
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

        // Use non-static buffers so each connection starts fresh
        char request_buffer[8192] = {0};
        int buffer_length = 0;

        for (;;) {
            char buffer[8192];
            ssize_t received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

            if (received == -1) {
                perror("Receive failed");
                break;
            }

            if (received == 0) {
                perror("Connection closed by the client");
                break;
            }

            if (buffer_length + received >= (int)sizeof(request_buffer)) {
                fprintf(stderr, "Error: Request too large\n");
                break;
            }

            memcpy(request_buffer + buffer_length, buffer, received);
            buffer_length += received;
            request_buffer[buffer_length] = '\0';

            // Attempt to parse requests as long as we find "\r\n\r\n"
            while (1) {
                char *result = strstr(request_buffer, "\r\n\r\n");
                if (!result) {
                    // No full header yet, read more data
                    break;
                }

                *result = '\0';
                char *body = result + 4;
                strncpy(global_body, body, sizeof(global_body) - 1);
                global_body[sizeof(global_body) - 1] = '\0';

                int status_code = check(request_buffer);

                if (status_code == -2) {
                    // Incomplete payload, don't send response, just break out of parsing loop
                    // and wait for more data in the next recv()
                    break;
                }

                // If we got here, we have a complete request:
                char response[16384];
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
                                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n%s",
                                        strlen(dynamic_resources[i].content), dynamic_resources[i].content);
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
                    log_dynamic_resources_to_file("debug_log.txt");
                } else if (status_code == 403) {
                    snprintf(response, sizeof(response), "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
                } else if (status_code == 204) {
                    snprintf(response, sizeof(response), "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
                    log_dynamic_resources_to_file("debug_log.txt");
                } else {
                    snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
                }

                // If status_code != -2, we have a response to send
                if (status_code != -2) {
                    int bytes_sent = send(client_sock, response, strlen(response), 0);
                    if (bytes_sent == -1) {
                        perror("Send failed");
                    }

                    // After processing one request, move the leftover data to the front
                    size_t request_size = (body - request_buffer) + strlen(global_body);
                    // request_size: Bytes consumed by the processed request (headers + body)
                    // Actually, we should carefully calculate:
                    // Headers end at result, result+4 is start of body.
                    // The total used = (result+4 - request_buffer) + content_length (if any)
                    // But since check() modifies global_body based on content length,
                    // we can assume all data up to global_body + content_length is processed.

                    // Let's compute how many bytes consumed exactly:
                    // This is a simplified approach:
                    // Find where headers ended: (result + 4)
                    // Add content_length (from the request)
                    // We'll need to re-parse with the actual content_length logic if necessary.
                    // For simplicity and since no multiple requests are handled simultaneously, let's do:
                    int used_bytes = (int)((result + 4) - request_buffer) + (int)strlen(global_body);

                    memmove(request_buffer, request_buffer + used_bytes, buffer_length - used_bytes);
                    buffer_length -= used_bytes;
                    request_buffer[buffer_length] = '\0';

                    memset(global_body, 0, sizeof(global_body));
                    memset(last_uri, 0, sizeof(last_uri));
                }

                // Check again if there's another complete request in the buffer
                // If incomplete request was detected, we break above
            }

            // If incomplete (-2) was detected above, we just continue to recv more data
            // No break/continue needed here, just go back to recv.
        }

        close(client_sock);
    }

    close(sock);
    return 0;
}
