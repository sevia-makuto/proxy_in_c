#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main()
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr;

    // create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }
    // configure server addreess
    memset(&server_addr, 0, sizeof(server_addr));
    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // star listenning
    if (listen(server_fd, 10) == -1)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port 8080....\n");

    // accept one client

    client_fd = accept(server_fd, NULL, NULL);

    if (client_fd == -1)
    {
        perror("accept");
        close(server_fd);
        return 1;
    }

    printf("client connected !!!!\n");

    // receive client message
    char buffer[1024] = {0};

    ssize_t bytes_received = recv(
        client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        printf("client says : %s\n", buffer);
    }
    // sens response

    const char *response = "hello from the c tcp server \n";
    send(client_fd, response, strlen(response), 0);

    // close socket
    close(client_fd);
    close(server_fd);

    return 0;
}