
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/sha.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define PORT 8090
#define BUFFER_SIZE 8192

/* Send all bytes, including when send() writes partially. */
int send_all(int fd, const void *data, size_t length)
{
    const char *bytes = data;
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            fd,
            bytes + total_sent,
            length - total_sent,
            0);

        if (sent < 0)
        {
            if (errno == EINTR)
                continue;

            perror("send");
            return -1;
        }

        if (sent == 0)
            return -1;

        total_sent += (size_t)sent;
    }

    return 0;
}

/* Send a simple HTTP error response to the browser. */
void send_error(int client_fd, int status, const char *message)
{
    char response[512];

    int length = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status,
        message,
        strlen(message),
        message);

    if (length > 0 && (size_t)length < sizeof(response))
        send_all(client_fd, response, (size_t)length);
}

/* Connect to an IPv4 origin server. */
int connect_origin(const char *hostname, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *address;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(
        hostname,
        port,
        &hints,
        &results);

    if (status != 0)
    {
        fprintf(stderr, "DNS error: %s\n", gai_strerror(status));
        return -1;
    }

    int origin_fd = -1;

    for (address = results; address != NULL; address = address->ai_next)
    {
        origin_fd = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);

        if (origin_fd == -1)
            continue;

        if (connect(
                origin_fd,
                address->ai_addr,
                address->ai_addrlen) == 0)
        {
            break;
        }

        close(origin_fd);
        origin_fd = -1;
    }

    freeaddrinfo(results);

    return origin_fd;
}
void generate_cache_key(const char *url, char *filename)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(
        (unsigned char *)url,
        strlen(url),
        hash);

    char hex_hash[SHA256_DIGEST_LENGTH * 2 + 1];

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(
            &hex_hash[i * 2],
            "%02x",
            hash[i]);
    }

    snprintf(
        filename,
        256,
        "cache/%s.cache",
        hex_hash);
}

/* Handle one browser request. */
void handle_client(int client_fd)
{
    char request[BUFFER_SIZE];
    size_t total_received = 0;

    printf("Receiving browser request...\n");

    /* Receive the complete HTTP request headers. */
    while (total_received < sizeof(request) - 1)
    {
        ssize_t received = recv(
            client_fd,
            request + total_received,
            sizeof(request) - 1 - total_received,
            0);

        if (received < 0)
        {
            if (errno == EINTR)
                continue;

            perror("recv request");
            return;
        }

        if (received == 0)
        {
            printf("Client disconnected.\n");
            return;
        }

        total_received += (size_t)received;
        request[total_received] = '\0';

        if (strstr(request, "\r\n\r\n") != NULL)
            break;
    }

    if (strstr(request, "\r\n\r\n") == NULL)
    {
        send_error(client_fd, 400, "Request headers too large or incomplete");
        return;
    }

    printf("\nBrowser request:\n%s\n", request);

    /* Extract method, destination URL and HTTP version. */
    char method[16];
    char target[4096];
    char version[16];

    if (sscanf(request, "%15s %4095s %15s",
               method, target, version) != 3)
    {
        send_error(client_fd, 400, "Invalid HTTP request");
        return;
    }

    /* This first version supports GET only. */
    if (strcmp(method, "GET") != 0)
    {
        send_error(client_fd, 501, "Only GET is supported");
        return;
    }

    /* This first version supports ordinary HTTP, not HTTPS. */
    if (strncmp(target, "http://", 7) != 0)
    {
        send_error(client_fd, 501, "Only ordinary HTTP URLs are supported");
        return;
    }

    /* Extract the authority (hostname and optional port). */
    char *authority = target + 7;
    char *path_start = strpbrk(authority, "/?#");

    size_t authority_length = path_start
                                  ? (size_t)(path_start - authority)
                                  : strlen(authority);

    char hostport[256];

    if (authority_length == 0 ||
        authority_length >= sizeof(hostport))
    {
        send_error(client_fd, 400, "Invalid hostname");
        return;
    }

    memcpy(hostport, authority, authority_length);
    hostport[authority_length] = '\0';

    /* Separate hostname and port. */
    char hostname[256];
    char port[6] = "80";

    char *colon = strrchr(hostport, ':');

    if (colon != NULL)
    {
        *colon = '\0';

        char *port_text = colon + 1;
        char *endptr;
        long port_number = strtol(port_text, &endptr, 10);

        if (*port_text == '\0' || *endptr != '\0' ||
            port_number < 1 || port_number > 65535 ||
            strlen(port_text) >= sizeof(port))
        {
            send_error(client_fd, 400, "Invalid port");
            return;
        }

        snprintf(port, sizeof(port), "%ld", port_number);
    }

    if (strlen(hostport) == 0 ||
        strlen(hostport) >= sizeof(hostname))
    {
        send_error(client_fd, 400, "Invalid hostname");
        return;
    }

    strcpy(hostname, hostport);

    /* Extract the resource path. */
    char path[4096];

    if (path_start == NULL || *path_start == '#')
    {
        strcpy(path, "/");
    }
    else if (*path_start == '?')
    {
        snprintf(path, sizeof(path), "/%s", path_start);
    }
    else
    {
        size_t path_length = strcspn(path_start, "#");

        if (path_length >= sizeof(path))
        {
            send_error(client_fd, 400, "Resource path too long");
            return;
        }

        memcpy(path, path_start, path_length);
        path[path_length] = '\0';
    }

    printf("Destination hostname: %s\n", hostname);
    printf("Destination port: %s\n", port);
    printf("Resource path: %s\n", path);

    /* Connect to the origin server. */
    printf("Connecting to origin server...\n");

    int origin_fd = connect_origin(hostname, port);

    if (origin_fd == -1)
    {
        send_error(client_fd, 502, "Could not connect to origin server");
        return;
    }

    printf("Origin server connected!\n");

    /* Build a new request for the origin server. */
    char origin_request[8192];

    int request_length = snprintf(
        origin_request,
        sizeof(origin_request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path,
        hostport);

    if (request_length < 0 ||
        (size_t)request_length >= sizeof(origin_request))
    {
        send_error(client_fd, 400, "Request too large");
        close(origin_fd);
        return;
    }

    /* Forward the request to the origin server. */
    printf("Forwarding request to origin...\n");

    if (send_all(origin_fd, origin_request, (size_t)request_length) == -1)
    {
        send_error(client_fd, 502, "Could not send origin request");
        close(origin_fd);
        return;
    }

    /* Receive origin response and forward its original bytes. */
    printf("Receiving origin response...\n");

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while (1)
    {
        bytes_received = recv(
            origin_fd,
            buffer,
            sizeof(buffer),
            0);

        if (bytes_received > 0)
        {
            if (send_all(client_fd, buffer,
                         (size_t)bytes_received) == -1)
            {
                printf("Browser disconnected during response.\n");
                break;
            }

            continue;
        }

        if (bytes_received == 0)
            break;

        if (errno == EINTR)
            continue;

        perror("recv origin");
        break;
    }

    close(origin_fd);

    printf("Request completed.\n");
}

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    int reuse = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == -1)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_address.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_address,
             sizeof(server_address)) == -1)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == -1)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("C HTTP Proxy listening on 127.0.0.1:%d\n", PORT);

    while (1)
    {
        client_fd = accept(server_fd, NULL, NULL);

        if (client_fd == -1)
        {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        printf("\nNew client connected!\n");

        handle_client(client_fd);

        close(client_fd);
        printf("Client connection closed.\n");
    }

    close(server_fd);
    return 0;
}
