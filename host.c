#include <stdio.h>
#include <string.h>

int main(void)
{
    char request[] =
        "GET /photo.jpg HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    // Find Host header
    char *host = strstr(request, "Host:");

    if (host == NULL)
    {
        printf("Host header not found\n");
        return 1;
    }

    // Move past Host:
    host += 5;

    // Skip spaces and tabs
    while (*host == ' ' || *host == '\t')
    {
        host++;
    }

    // Find end of header
    char *end = strstr(host, "\r\n");

    if (end == NULL)
    {
        printf("Invalid Host header\n");
        return 1;
    }

    // Terminate hostname
    *end = '\0';

    printf("Hostname: %s\n", host);

    return 0;
}