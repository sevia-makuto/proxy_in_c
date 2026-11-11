
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int send_all(int fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            fd,
            data + total_sent,
            length - total_sent,
            0);

        if (sent < 0)
        {
            if (errno == EINTR)
                continue;

            perror("Send failed");
            return -1;
        }

        if (sent == 0)
        {
            fprintf(stderr, "Connection closed while sending.\n");
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

int main(void)
{
    const char *hostname = "example.com";
    const char *port = "80";

    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *address;

    int server_fd = -1;

    // 1. Configure DNS lookup
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // 2. Resolve hostname
    printf("Resolving hostname...\n");

    int status = getaddrinfo(
        hostname,
        port,
        &hints,
        &results);

    if (status != 0)
    {
        fprintf(stderr, "DNS error: %s\n", gai_strerror(status));
        return 1;
    }

    printf("DNS resolution successful!\n");

    // 3. Create socket and connect
    for (address = results; address != NULL; address = address->ai_next)
    {
        server_fd = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);

        if (server_fd == -1)
        {
            perror("Socket failed");
            continue;
        }

        if (connect(
                server_fd,
                address->ai_addr,
                address->ai_addrlen) == 0)
        {
            printf("Connected to origin server!\n");
            break;
        }

        perror("Connect failed");
        close(server_fd);
        server_fd = -1;
    }

    freeaddrinfo(results);

    if (server_fd == -1)
    {
        fprintf(stderr, "Could not connect to origin server.\n");
        return 1;
    }

    // 4. Prepare HTTP GET request
    char request[512];

    int request_length = snprintf(
        request,
        sizeof(request),
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        hostname);

    if (request_length < 0 ||
        (size_t)request_length >= sizeof(request))
    {
        fprintf(stderr, "Could not prepare HTTP request.\n");
        close(server_fd);
        return 1;
    }

    // 5. Send HTTP request
    printf("Sending HTTP GET request...\n");

    if (send_all(server_fd, request, (size_t)request_length) == -1)
    {
        close(server_fd);
        return 1;
    }

    printf("Request sent successfully!\n\n");

    // 6. Receive HTTP response
    printf("Receiving HTTP response...\n\n");

    char buffer[4096];
    ssize_t bytes_received;

    while (1)
    {
        bytes_received = recv(
            server_fd,
            buffer,
            sizeof(buffer),
            0);

        if (bytes_received > 0)
        {
            fwrite(buffer, 1, (size_t)bytes_received, stdout);
            continue;
        }

        if (bytes_received == 0)
        {
            printf("\n\nServer closed connection.\n");
            break;
        }

        if (errno == EINTR)
            continue;

        perror("Receive failed");
        break;
    }

    // 7. Close connection
    close(server_fd);

    printf("\nConnection closed.\n");

    return 0;
}
