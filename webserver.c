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

DynamicResource dynamic_resources[100];
int resource_count = 0;

void log_dynamic_resources_to_file(const char *filename) {
    FILE *file = fopen(filename, "a");
    if (!file) {
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

    fclose(file);
}

void log_request_and_status_to_file(const char *filename, const char *request_buffer, int status_code, const char *response) {
    FILE *file = fopen(filename, "a");
    if (!file) {
        perror("Failed to open file for logging request and status");
        return;
    }

    fprintf(file, "Received request buffer:\n%s\n", request_buffer);
    fprintf(file, "Status code: %d\n", status_code);
    fprintf(file, "Response: %s\n\n", response);

    fclose(file);
}


// Parses request headers and determines initial status, deferring PUT resource creation/update until body is read.
int check_request(char *str, int *content_length_out) {
    if (!str) return -1;

    char method[16], version[16];
    int content_length = 0;
    *content_length_out = 0;

    char *line = strtok(str, "\r\n");
    if (!line || sscanf(line, "%15s %255s %15s", method, last_uri, version) != 3) {
        return 400;
    }

    int header_count = 0;
    char *header = strtok(NULL, "\r\n");
    while (header) {
        if (header_count >= 40) return 400;
        header_count++;

        char *delimiter = strchr(header, ':');
        if (!delimiter) return 400;

        *delimiter = '\0';
        char *key = header;
        char *value = delimiter + 1;
        while (*value == ' ') value++;

        if (strlen(key) > 256 || strlen(value) > 256) return 400;

        if (strcasecmp(key, "Content-Length") == 0) {
            content_length = atoi(value);
            if (content_length > 8192) return 413; // Payload too large
        }

        header = strtok(NULL, "\r\n");
    }

    *content_length_out = content_length;

    // Support GET, PUT, DELETE, others => 501
    if (strcmp(method, "GET") == 0) {
        if (strncmp(last_uri, "/static/", 8) == 0) {
            char *resource_name = last_uri + 8;
            if (!strcmp(resource_name, "foo") ||
                !strcmp(resource_name, "bar") ||
                !strcmp(resource_name, "baz")) {
                return 200;
            } else {
                return 404;
            }
        } else if (strncmp(last_uri, "/dynamic/", 9) == 0) {
            char *resource_name = last_uri + 9;
            for (int i = 0; i < resource_count; i++) {
                if (!strcmp(dynamic_resources[i].path, resource_name)) {
                    return 200;
                }
            }
            return 404;
        } else {
            return 404;
        }

    } else if (strcmp(method, "PUT") == 0) {
        // Defer final PUT status until after reading the body
        if (!strncmp(last_uri, "/dynamic/", 9)) {
            return 0; // indicates deferred decision
        }
        return 403;

    } else if (strcmp(method, "DELETE") == 0) {
        if (!strncmp(last_uri, "/dynamic/", 9)) {
            char *resource_name = last_uri + 9;
            for (int i = 0; i < resource_count; i++) {
                if (!strcmp(dynamic_resources[i].path, resource_name)) {
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
        return 501;
    }

    return 0;
}

static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
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
        printf("Invalid port number.\n");
        return 1;
    }

    struct sockaddr_in server_addr = derive_sockaddr(host, port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Socket creation failed: %s\n", strerror(errno));
        return 1;
    }

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

    if (listen(sock, 10) == -1) {
        printf("Listen failed: %s\n", strerror(errno));
        close(sock);
        return 1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock == -1) {
        perror("Accept failed");
        close(sock);
        return 1;
    }

    // Increase timeout or remove it to ensure the server stays open long enough
    // Let's just remove the timeout for stability
    // struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    // setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char request_buffer[8192] = {0};
    int buffer_length = 0;

    for (;;) {
        char buffer[8192];
        ssize_t received = recv(client_sock, buffer, sizeof(buffer), 0);
        if (received == -1) {
            // If a timeout or EAGAIN occurs, just continue waiting
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Continue waiting for next request
                continue;
            } else {
                perror("Receive failed");
                break; // close connection on fatal error
            }
        }

        if (received == 0) {
            // Client closed connection
            break;
        }

        if (buffer_length + (int)received > (int)sizeof(request_buffer)) {
            // Request too large, respond and continue
            const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            send(client_sock, resp, strlen(resp), 0);
            // Clear buffer and continue to next request
            buffer_length = 0;
            request_buffer[0] = '\0';
            continue;
        }

        memcpy(request_buffer + buffer_length, buffer, received);
        buffer_length += (int)received;
        request_buffer[buffer_length] = '\0';

        // Process all complete requests in the buffer
        for (;;) {
            char *end_of_headers = strstr(request_buffer, "\r\n\r\n");
            if (!end_of_headers) {
                // Not enough data for headers
                break;
            }

            int headers_len = (int)((end_of_headers + 4) - request_buffer);

            int content_length = 0;
            char saved_char = *end_of_headers;
            *end_of_headers = '\0';
            int status_code = check_request(request_buffer, &content_length);
            *end_of_headers = saved_char;

            int total_needed = headers_len + content_length;
            if (buffer_length < total_needed) {
                // Not enough data for full body yet
                break;
            }

            char *body = request_buffer + headers_len;
            memset(global_body, 0, sizeof(global_body));
            if (content_length > 0) {
                if (content_length > (int)sizeof(global_body)-1) {
                    status_code = 413; // Payload too large
                    content_length = 0;
                } else {
                    memcpy(global_body, body, content_length);
                    global_body[content_length] = '\0';
                }
            } else {
                global_body[0] = '\0';
            }

            // Finalize PUT if deferred
            if (status_code == 0 && strncmp(last_uri, "/dynamic/", 9) == 0) {
                char *resource_name = last_uri + 9;
                int found = 0;
                for (int i = 0; i < resource_count; i++) {
                    if (!strcmp(dynamic_resources[i].path, resource_name)) {
                        strncpy(dynamic_resources[i].content, global_body, 8191);
                        dynamic_resources[i].content[8191] = '\0';
                        status_code = 204; // updated
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (resource_count < 100) {
                        strncpy(dynamic_resources[resource_count].path, resource_name, 255);
                        dynamic_resources[resource_count].path[255] = '\0';
                        strncpy(dynamic_resources[resource_count].content, global_body, 8191);
                        dynamic_resources[resource_count].content[8191] = '\0';
                        resource_count++;
                        status_code = 201; // created
                    } else {
                        status_code = 403;
                    }
                }
            }

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
                    if (!strcmp(resource_name, "foo")) {
                        snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nFoo");
                    } else if (!strcmp(resource_name, "bar")) {
                        snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nBar");
                    } else if (!strcmp(resource_name, "baz")) {
                        snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nBaz");
                    } else {
                        snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                    }
                } else if (strncmp(last_uri, "/dynamic/", 9) == 0) {
                    char *resource_name = last_uri + 9;
                    int found = 0;
                    for (int i = 0; i < resource_count; i++) {
                        if (!strcmp(dynamic_resources[i].path, resource_name)) {
                            snprintf(response, sizeof(response),
                                     "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n%s",
                                     (long)strlen(dynamic_resources[i].content),
                                     dynamic_resources[i].content);
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                    }
                } else {
                    snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
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
                // If status_code == 0 or unknown, treat as 500
                snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
            }

            log_request_and_status_to_file("debug_log.txt", request_buffer, status_code, response);

            send(client_sock, response, strlen(response), 0);

            // Remove headers + body
            int remove_len = total_needed;
            if (remove_len > buffer_length) {
                remove_len = buffer_length;
            }
            memmove(request_buffer, request_buffer + remove_len, buffer_length - remove_len);
            buffer_length -= remove_len;
            request_buffer[buffer_length] = '\0';
            memset(global_body, 0, sizeof(global_body));
            memset(last_uri, 0, sizeof(last_uri));
        }
    }

    close(client_sock);
    close(sock);
    return 0;
}
