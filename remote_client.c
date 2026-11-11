
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

// Function to send all bytes
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

            perror("send failed");
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

    // STEP 1: Configure DNS lookup
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    printf("[1] Resolving hostname: %s\n", hostname);
    fflush(stdout);

    int status = getaddrinfo(
        hostname,
        port,
        &hints,
        &results);

    if (status != 0)
    {
        fprintf(stderr, "DNS error: %s\n",
                gai_strerror(status));
        return 1;
    }

    printf("[SUCCESS] Hostname resolved.\n");

    // STEP 2: Create socket and connect
    for (address = results;
         address != NULL;
         address = address->ai_next)
    {
        printf("[2] Creating socket...\n");
        fflush(stdout);

        server_fd = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);

        if (server_fd == -1)
        {
            perror("socket failed");
            continue;
        }

        printf("[SUCCESS] Socket created.\n");
        printf("[3] Connecting to remote server...\n");
        fflush(stdout);

        if (connect(
                server_fd,
                address->ai_addr,
                address->ai_addrlen) == 0)
        {
            printf("[SUCCESS] TCP connection established!\n");
            break;
        }

        perror("connect failed");

        close(server_fd);
        server_fd = -1;
    }

    freeaddrinfo(results);

    if (server_fd == -1)
    {
        fprintf(stderr, "Could not connect to %s:%s\n",
                hostname, port);
        return 1;
    }

    // STEP 3: Prepare HTTP request
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

    printf("[4] Sending HTTP request...\n");

    // STEP 4: Send request
    if (send_all(
            server_fd,
            request,
            (size_t)request_length) == -1)
    {
        close(server_fd);
        return 1;
    }

    printf("[SUCCESS] HTTP request sent.\n");
    printf("[5] Receiving HTTP response...\n\n");

    // STEP 5: Receive response
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
            // Write exactly the received bytes
            fwrite(buffer, 1,
                   (size_t)bytes_received, stdout);

            continue;
        }

        if (bytes_received == 0)
        {
            printf("\n\n[SUCCESS] Server closed connection.\n");
            break;
        }

        if (errno == EINTR)
            continue;

        perror("recv failed");
        break;
    }

    // STEP 6: Close connection
    close(server_fd);

    printf("[6] Connection closed.\n");

    return 0;
}
