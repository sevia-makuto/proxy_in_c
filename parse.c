#include <stdio.h>
#include <string.h>

int main()
{
    char requests[] =
        "GET /photo.jpg HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    // char *end_of_headers = strstr(requests, "\r\n\r\n");

    // if (end_of_headers != NULL)
    // {
    //     printf("Http headers found successfully!\n");
    // }
    // else
    // {
    //     printf("End of  headers not found.\n");
    // }

    // return 0;

    // extract the requeted line
    char *request_line_end = strstr(requests, "\r\n");
    if (request_line_end == NULL)
    {
        printf("Invalid HTTP request\n");
        return 1;
    }
    // terminate the requested line
    *request_line_end = '\0';
    // Extract method
    char *method = strtok(requests, " ");

    // Extract path
    char *path = strtok(NULL, " ");

    // Extract HTTP version
    char *version = strtok(NULL, " ");

    if (method == NULL || path == NULL || version == NULL)
    {
        printf("Invalid request line\n");
        return 1;
    }

    printf("Method: %s\n", method);
    printf("Path: %s\n", path);
    printf("Version: %s\n", version);

    return 0;
}