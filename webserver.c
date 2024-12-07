#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
    };
    struct addrinfo *result_info;
    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
        fprintf(stderr, "Error parsing host/port\n");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in result = *((struct sockaddr_in*) result_info->ai_addr);
    freeaddrinfo(result_info);
    return result;
}

typedef struct {
    char* path;
    char* cont;
} Resource;

#define MAX_RESOURCES 100
Resource resources[MAX_RESOURCES];
size_t resCount = 0;
char response[8192];

const char* get_static_content(const char* uri) {
    if (strcmp(uri, "/static/foo") == 0) return "Foo";
    else if (strcmp(uri, "/static/bar") == 0) return "Bar";
    else if (strcmp(uri, "/static/baz") == 0) return "Baz";
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        return 1;
    }

    char *myIP = argv[1];
    char *myPort = argv[2];

    int intPort = atoi(myPort);

    if (intPort > 65535 || intPort < 1) {
        printf("Not correct number of port\n");
        return 1;
    }

    struct sockaddr_in myStruct = derive_sockaddr(myIP, myPort);
    int mySocket = socket(AF_INET, SOCK_STREAM, 0);
    if (mySocket == -1) {
        printf("Socket wasn't created properly\n");
        return 1;
    }
    socklen_t myLen = sizeof(myStruct);

    int opt = 1;

    if (setsockopt(mySocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Failed to set SO_REUSEADDR");
        close(mySocket);
        return 1;
    }

    int myBind = bind(mySocket, (struct sockaddr*)&myStruct, sizeof(myStruct));
    if (myBind == -1) {
        printf("Bind wasn't created properly\n");
        close(mySocket);
        return 1;
    }
    printf("Binding\n");

    int myListen = listen(mySocket, 10);
    if (myListen == -1) {
        printf("Listen wasn't created properly\n");
        close(mySocket);
        return 1;
    }
    printf("Listening\n");

    while (1) {
        int myAccept = accept(mySocket, (struct sockaddr*)&myStruct, &myLen);
        if (myAccept == -1) {
            printf("Accept wasn't created properly\n");
            close(mySocket);
            return 1;
        }
        printf("Accepting\n");

        char buf[8192];
        size_t bufLen = 0;
        ssize_t myRecv;
        int isConClosed = 0;

        while (!isConClosed) {
            myRecv = recv(myAccept, buf + bufLen, sizeof(buf) - bufLen - 1, 0);
            if (myRecv <= 0) {
                if (myRecv == 0) printf("Client disconnected\n");
                else fprintf(stderr, "Receive error\n");
                break;
            }
            bufLen += myRecv;
            buf[bufLen] = '\0';

            if (bufLen > 8192) {
                const char *response =
                    "HTTP/1.1 413 Payload Too Large\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n";
                send(myAccept, response, strlen(response), 0);
                isConClosed = 1;
                break;
            }

            size_t elReady = 0;
            while (1) {
                char *reqEnd = strstr(buf + elReady, "\r\n\r\n");
                if (reqEnd == NULL) break;

                size_t headerLen = reqEnd - (buf + elReady) + 4;

                if (headerLen > 8192) {
                    const char *response =
                        "HTTP/1.1 431 Request Header Fields Too Large\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n";
                    send(myAccept, response, strlen(response), 0);
                    isConClosed = 1;
                    break;
                }

                char request[headerLen + 1];
                memcpy(request, buf + elReady, headerLen);
                request[headerLen] = '\0';

                char *lines[40];
                int lineCount = 0;
                char *line = strtok(request, "\r\n");
                while (line && lineCount < 40) {
                    lines[lineCount++] = line;
                    line = strtok(NULL, "\r\n");
                }

                if (lineCount < 1) {
                    const char *response =
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 11\r\n"
                        "\r\n"
                        "Bad Request";
                    send(myAccept, response, strlen(response), 0);
                    isConClosed = 1;
                    break;
                }

                char method[16] = {0};
                char uri[256] = {0};
                char httpVersion[16] = {0};

                if (sscanf(lines[0], "%15s %255s %15s", method, uri, httpVersion) != 3) {
                    const char *response =
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 11\r\n"
                        "\r\n"
                        "Bad Request";
                    send(myAccept, response, strlen(response), 0);
                    isConClosed = 1;
                    break;
                }

                int contLen = 0;
                for (int i = 1; i < lineCount; i++) {
                    char headerName[256] = {0};
                    char headerVal[256] = {0};

                    if (sscanf(lines[i], "%255[^:]: %255[^\r\n]", headerName, headerVal) == 2) {
                        if (strcasecmp(headerName, "Content-Length") == 0) contLen = atoi(headerVal);
                    }
                }

                if (contLen > 8192) {
                    const char *response =
                        "HTTP/1.1 413 Payload Too Large\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n";
                    send(myAccept, response, strlen(response), 0);
                    isConClosed = 1;
                    break;
                }

                size_t reqLen = headerLen + contLen;
                if (reqLen > 8192) {
                    const char *response =
                        "HTTP/1.1 413 Payload Too Large\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n";
                    send(myAccept, response, strlen(response), 0);
                    isConClosed = 1;
                    break;
                }

                if (bufLen - elReady < reqLen) break;

                char *body = NULL;
                if (contLen > 0) {
                    body = malloc(contLen + 1);
                    if (body == NULL) {
                        const char *response =
                            "HTTP/1.1 500 Internal Server Error\r\n"
                            "Content-Length: 0\r\n"
                            "\r\n";
                        send(myAccept, response, strlen(response), 0);
                        isConClosed = 1;
                        break;
                    }
                    memcpy(body, buf + elReady + headerLen, contLen);
                    body[contLen] = '\0';
                }

                if (strcmp(method, "GET") == 0) {
                    const char* content = get_static_content(uri);
                    if (content) {
                        snprintf(response, sizeof(response),
                                 "HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "Content-Length: %ld\r\n"
                                 "\r\n"
                                 "%s",
                                 strlen(content), content);
                        send(myAccept, response, strlen(response), 0);
                        printf("Sent response: 200 OK\n");
                    } else {
                        int resFound = 0;
                        for (size_t i = 0; i < resCount; i++) {
                            if (strcmp(resources[i].path, uri) == 0) {
                                if (strlen(resources[i].cont) > 8192) {
                                    const char *response =
                                        "HTTP/1.1 500 Internal Server Error\r\n"
                                        "Content-Length: 0\r\n"
                                        "\r\n";
                                    send(myAccept, response, strlen(response), 0);
                                    isConClosed = 1;
                                    break;
                                }
                                snprintf(response, sizeof(response),
                                         "HTTP/1.1 200 OK\r\n"
                                         "Content-Type: text/plain\r\n"
                                         "Content-Length: %ld\r\n"
                                         "\r\n"
                                         "%s",
                                         strlen(resources[i].cont), resources[i].cont);
                                send(myAccept, response, strlen(response), 0);
                                resFound = 1;
                                break;
                            }
                        }
                        if (!resFound) {
                            const char *response =
                                "HTTP/1.1 404 Not Found\r\n"
                                "Content-Length: 0\r\n"
                                "\r\n";
                            send(myAccept, response, strlen(response), 0);
                        }
                    }
                } else if (strcmp(method, "PUT") == 0) {
                    int resFound = 0;
                    for (size_t i = 0; i < resCount; i++) {
                        if (strcmp(resources[i].path, uri) == 0) {
                            free(resources[i].cont);
                            resources[i].cont = strdup(body ? body : "");
                            resFound = 1;
                            const char *response =
                                "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
                            send(myAccept, response, strlen(response), 0);
                            break;
                        }
                    }

                    if (!resFound) {
                        if (resCount < MAX_RESOURCES) {
                            resources[resCount].path = strdup(uri);
                            resources[resCount].cont = strdup(body ? body : "");
                            resCount++;
                            const char *response =
                                "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
                            send(myAccept, response, strlen(response), 0);
                        } else {
                            const char *response =
                                "HTTP/1.1 507 Insufficient Storage\r\nContent-Length: 0\r\n\r\n";
                            send(myAccept, response, strlen(response), 0);
                        }
                    }
                } else if (strcmp(method, "DELETE") == 0) {
                    int resFound = 0;
                    for (size_t i = 0; i < resCount; i++) {
                        if (strcmp(resources[i].path, uri) == 0) {
                            free(resources[i].path);
                            free(resources[i].cont);
                            for (size_t j = i; j < resCount - 1; j++) {
                                resources[j] = resources[j + 1];
                            }
                            resCount--;
                            resFound = 1;
                            const char *response =
                                "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
                            send(myAccept, response, strlen(response), 0);
                            break;
                        }
                    }
                    if (!resFound) {
                        const char *response =
                            "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                        send(myAccept, response, strlen(response), 0);
                    }
                } else {
                    const char *response =
                        "HTTP/1.1 501 Not Implemented\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n";
                    send(myAccept, response, strlen(response), 0);
                }

                if (body) free(body);

                elReady += reqLen;
                if (isConClosed) break;
                if (elReady >= bufLen) {
                    bufLen = 0;
                    elReady = 0;
                    break;
                }
            }

            if (isConClosed) break;
            if (elReady < bufLen) {
                memmove(buf, buf + elReady, bufLen - elReady);
                bufLen -= elReady;
            } else bufLen = 0;
        }

        close(myAccept);
    }

    close(mySocket);
    return 0;
}
