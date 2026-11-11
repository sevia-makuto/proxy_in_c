
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stddef.h>
#include <strings.h>

#define PORT 8090
#define BUFFER_SIZE 8192
#define MAX_HEADER_SIZE 16384
typedef struct
{
    int no_store;
    int is_private;
    int no_cache;
    int must_revalidate;

    long max_age;
    long s_maxage;
    long freshness_lifetime;

} CachePolicy;

int cache_exists(const char *filename);

unsigned char *read_cache_file(
    const char *filename,
    size_t *length);

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
// int extract_max_age(const char *headers)
// {
//     const char *cache_control =
//         strcasestr(headers, "Cache-Control:");

//     if (cache_control == NULL)
//     {
//         return -1;
//     }

//     const char *line_end = strstr(cache_control, "\r\n");

//     if (line_end == NULL)
//     {
//         return -1;
//     }

//     const char *max_age =
//         strcasestr(cache_control, "max-age=");

//     if (max_age == NULL || max_age >= line_end)
//     {
//         return -1;
//     }

//     max_age += strlen("max-age=");

//     char *endptr;
//     long ttl = strtol(max_age, &endptr, 10);

//     if (endptr == max_age ||
//         endptr > line_end ||
//         ttl <= 0 ||
//         ttl > 2147483647L)
//     {
//         return -1;
//     }

//     return (int)ttl;
// }
// int has_no_store(const char *headers)
// {
//     const char *cache_control =
//         strcasestr(headers, "Cache-Control:");

//     if (cache_control == NULL)
//     {
//         return 0;
//     }

//     const char *line_end = strstr(cache_control, "\r\n");

//     if (line_end == NULL)
//     {
//         return 0;
//     }

//     const char *no_store =
//         strcasestr(cache_control, "no-store");

//     if (no_store == NULL || no_store >= line_end)
//     {
//         return 0;
//     }

//     return 1;
// }
// int should_cache_response(const char *headers, int *ttl)
// {
//     /* Check HTTP status */
//     if (strncmp(headers, "HTTP/1.1 200", 12) != 0 &&
//         strncmp(headers, "HTTP/1.0 200", 12) != 0)
//     {
//         return 0;
//     }

//     /* Reject responses containing no-store */
//     if (has_no_store(headers))
//     {
//         return 0;
//     }

//     /* Extract max-age */
//     *ttl = extract_max_age(headers);

//     if (*ttl <= 0)
//     {
//         return 0;
//     }

//     return 1;
// }
/* Remove leading and trailing whitespace from a string. */
char *trim_whitespace(char *text)
{
    while (*text == ' ' || *text == '\t')
    {
        text++;
    }

    char *end = text + strlen(text);

    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t'))
    {
        end--;
    }

    *end = '\0';

    return text;
}

/* Parse a non-negative Cache-Control number. */
int parse_delta_seconds(char *value, long *result)
{
    value = trim_whitespace(value);

    size_t length = strlen(value);

    /* Accept quoted numeric values. */
    if (length >= 2 &&
        value[0] == '"' &&
        value[length - 1] == '"')
    {
        value[length - 1] = '\0';
        value++;
    }

    if (*value == '\0')
        return -1;

    errno = 0;

    char *endptr;
    long number = strtol(value, &endptr, 10);

    if (errno == ERANGE || endptr == value || number < 0)
        return -1;

    endptr = trim_whitespace(endptr);

    if (*endptr != '\0')
        return -1;

    *result = number;

    return 0;
}

/* Parse one Cache-Control header value. */
int parse_cache_control_value(char *value, CachePolicy *policy)
{
    char *saveptr = NULL;

    char *directive = strtok_r(value, ",", &saveptr);

    while (directive != NULL)
    {
        directive = trim_whitespace(directive);

        char *equals = strchr(directive, '=');

        char *directive_value = NULL;

        if (equals != NULL)
        {
            *equals = '\0';
            directive_value = trim_whitespace(equals + 1);
        }

        char *name = trim_whitespace(directive);

        if (strcasecmp(name, "no-store") == 0)
        {
            policy->no_store = 1;
        }
        else if (strcasecmp(name, "private") == 0)
        {
            policy->is_private = 1;
        }
        else if (strcasecmp(name, "no-cache") == 0)
        {
            policy->no_cache = 1;
        }
        else if (strcasecmp(name, "must-revalidate") == 0)
        {
            policy->must_revalidate = 1;
        }
        else if (strcasecmp(name, "max-age") == 0)
        {
            if (directive_value == NULL ||
                parse_delta_seconds(
                    directive_value,
                    &policy->max_age) == -1)
            {
                return -1;
            }
        }
        else if (strcasecmp(name, "s-maxage") == 0)
        {
            if (directive_value == NULL ||
                parse_delta_seconds(
                    directive_value,
                    &policy->s_maxage) == -1)
            {
                return -1;
            }
        }

        directive = strtok_r(NULL, ",", &saveptr);
    }

    return 0;
}

/* Parse all Cache-Control headers in an HTTP response. */
int parse_cache_policy(
    const char *headers,
    CachePolicy *policy)
{
    memset(policy, 0, sizeof(*policy));

    policy->max_age = -1;
    policy->s_maxage = -1;
    policy->freshness_lifetime = -1;

    int found_cache_control = 0;

    /* Skip the HTTP status line. */
    const char *cursor = strstr(headers, "\r\n");

    if (cursor == NULL)
        return -1;

    cursor += 2;

    while (1)
    {
        const char *line_end = strstr(cursor, "\r\n");

        if (line_end == NULL)
            return -1;

        /* End of HTTP headers. */
        if (line_end == cursor)
            break;

        size_t line_length = (size_t)(line_end - cursor);

        if (line_length >= 4096)
            return -1;

        char line[4096];

        memcpy(line, cursor, line_length);
        line[line_length] = '\0';

        if (strncasecmp(line, "Cache-Control:", 14) == 0)
        {
            found_cache_control = 1;

            char *value = line + 14;

            if (parse_cache_control_value(value, policy) == -1)
                return -1;
        }

        cursor = line_end + 2;
    }

    if (!found_cache_control)
        return 0;

    /*
     * Shared caches use s-maxage when present.
     * Otherwise, use max-age.
     */
    if (policy->s_maxage >= 0)
    {
        policy->freshness_lifetime = policy->s_maxage;
    }
    else
    {
        policy->freshness_lifetime = policy->max_age;
    }

    return 0;
}

/* Decide whether an origin response may be stored. */
int should_cache_response(
    const char *headers,
    CachePolicy *policy)
{
    char protocol[16];
    int status_code;

    if (sscanf(
            headers,
            "%15s %d",
            protocol,
            &status_code) != 2)
    {
        return 0;
    }

    if ((strcmp(protocol, "HTTP/1.1") != 0 &&
         strcmp(protocol, "HTTP/1.0") != 0) ||
        status_code != 200)
    {
        return 0;
    }

    if (parse_cache_policy(headers, policy) == -1)
        return 0;

    /* Never store prohibited shared-cache responses. */
    if (policy->no_store || policy->is_private)
        return 0;

    /* We need a defined freshness lifetime for this stage. */
    if (policy->freshness_lifetime < 0)
        return 0;

    return 1;
}
int cache_is_fresh(const char *filename, long ttl)
{
    struct stat file_info;

    if (stat(filename, &file_info) != 0)
    {
        return 0;
    }

    time_t current_time = time(NULL);

    double elapsed = difftime(
        current_time,
        file_info.st_mtime);

    return elapsed < ttl;
}
size_t find_header_end(const unsigned char *data, size_t length)
{
    for (size_t i = 0; i + 3 < length; i++)
    {
        if (data[i] == '\r' &&
            data[i + 1] == '\n' &&
            data[i + 2] == '\r' &&
            data[i + 3] == '\n')
        {
            return i + 4;
        }
    }

    return 0;
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
        send_error(
            client_fd,
            400,
            "Request headers too large or incomplete");
        return;
    }

    printf("\nBrowser request:\n%s\n", request);

    /* Extract method, destination URL and HTTP version. */
    char method[16];
    char target[4096];
    char version[16];

    if (sscanf(
            request,
            "%15s %4095s %15s",
            method,
            target,
            version) != 3)
    {
        send_error(client_fd, 400, "Invalid HTTP request");
        return;
    }

    /* Support GET only. */
    if (strcmp(method, "GET") != 0)
    {
        send_error(client_fd, 501, "Only GET is supported");
        return;
    }

    /* Support ordinary HTTP, not HTTPS CONNECT. */
    if (strncmp(target, "http://", 7) != 0)
    {
        send_error(
            client_fd,
            501,
            "Only ordinary HTTP URLs are supported");
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

        if (*port_text == '\0' ||
            *endptr != '\0' ||
            port_number < 1 ||
            port_number > 65535 ||
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

    /* Prepare cache filenames. */
    char cache_filename[256];
    char temp_filename[300];

    size_t cached_length = 0;

    if (mkdir("cache", 0700) == -1 && errno != EEXIST)
    {
        perror("Creating cache directory");
        return;
    }

    generate_cache_key(target, cache_filename);

    snprintf(
        temp_filename,
        sizeof(temp_filename),
        "%s.tmp",
        cache_filename);

    /* =========================================
       CACHE LOOKUP
       ========================================= */

    if (cache_exists(cache_filename))
    {
        unsigned char *cached_response =
            read_cache_file(cache_filename, &cached_length);

        if (cached_response != NULL)
        {
            /*
             * Find the HTTP header boundary using
             * the actual binary response length.
             */
            size_t header_end = find_header_end(
                cached_response,
                cached_length);

            if (header_end > 0 &&
                header_end <= MAX_HEADER_SIZE)
            {
                /*
                 * Make a null-terminated copy of
                 * the complete HTTP header section.
                 */
                char *headers = malloc(header_end + 1);

                if (headers != NULL)
                {
                    memcpy(headers, cached_response, header_end);
                    headers[header_end] = '\0';

                    CachePolicy policy;

                    /*
                     * A no-cache response must not be
                     * reused without validation.
                     *
                     * Actual ETag validation will be
                     * implemented in the next stage.
                     */
                    if (should_cache_response(headers, &policy) &&
                        !policy.no_cache &&
                        cache_is_fresh(
                            cache_filename,
                            policy.freshness_lifetime))
                    {
                        printf(
                            "CACHE HIT: Serving cached response.\n");

                        if (send_all(
                                client_fd,
                                cached_response,
                                cached_length) == -1)
                        {
                            printf(
                                "Failed to send cached response.\n");
                        }

                        free(headers);
                        free(cached_response);
                        return;
                    }

                    free(headers);
                }
            }

            free(cached_response);
        }
    }

    printf("CACHE MISS: Contacting origin server.\n");

    /* =========================================
       CONNECT TO ORIGIN SERVER
       ========================================= */

    int origin_fd = connect_origin(hostname, port);

    if (origin_fd == -1)
    {
        send_error(
            client_fd,
            502,
            "Could not connect to origin server");
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

    if (send_all(
            origin_fd,
            origin_request,
            (size_t)request_length) == -1)
    {
        send_error(
            client_fd,
            502,
            "Could not send origin request");

        close(origin_fd);
        return;
    }

    /* =========================================
       RECEIVE ORIGIN RESPONSE
       ========================================= */

    printf("Receiving origin response...\n");

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    /*
     * Temporarily collect the initial response bytes
     * so that we can inspect the HTTP headers.
     */
    unsigned char initial_data[MAX_HEADER_SIZE + BUFFER_SIZE];

    size_t initial_length = 0;

    int headers_checked = 0;
    int response_complete = 0;

    FILE *cache_file = NULL;

    while (1)
    {
        bytes_received = recv(
            origin_fd,
            buffer,
            sizeof(buffer),
            0);

        if (bytes_received > 0)
        {
            /* Forward response bytes to the browser. */
            if (send_all(
                    client_fd,
                    buffer,
                    (size_t)bytes_received) == -1)
            {
                printf(
                    "Browser disconnected during response.\n");

                break;
            }

            /* Inspect the response headers once. */
            if (!headers_checked)
            {
                size_t received = (size_t)bytes_received;

                if (received >
                    sizeof(initial_data) - initial_length)
                {
                    headers_checked = 1;
                }
                else
                {
                    memcpy(
                        initial_data + initial_length,
                        buffer,
                        received);

                    initial_length += received;

                    size_t header_end = find_header_end(
                        initial_data,
                        initial_length);

                    if (header_end > 0)
                    {
                        headers_checked = 1;

                        if (header_end <= MAX_HEADER_SIZE)
                        {
                            char response_headers[MAX_HEADER_SIZE + 1];

                            memcpy(
                                response_headers,
                                initial_data,
                                header_end);

                            response_headers[header_end] = '\0';

                            CachePolicy policy;

                            /*
                             * Decide whether the origin
                             * response can be cached.
                             */
                            if (should_cache_response(
                                    response_headers,
                                    &policy))
                            {
                                cache_file = fopen(
                                    temp_filename,
                                    "wb");

                                if (cache_file != NULL)
                                {
                                    printf(
                                        "CACHE STORE: Eligible. "
                                        "Freshness lifetime: %ld seconds\n",
                                        policy.freshness_lifetime);

                                    /*
                                     * Save all bytes received
                                     * so far, including any
                                     * response body bytes.
                                     */
                                    if (fwrite(
                                            initial_data,
                                            1,
                                            initial_length,
                                            cache_file) != initial_length)
                                    {
                                        printf(
                                            "Cache write failed.\n");

                                        fclose(cache_file);
                                        cache_file = NULL;

                                        unlink(temp_filename);
                                    }
                                }
                                else
                                {
                                    perror(
                                        "Opening temporary cache file");
                                }
                            }
                            else
                            {
                                printf(
                                    "CACHE RULE: Response not eligible.\n");
                            }
                        }
                    }
                    else if (initial_length > MAX_HEADER_SIZE)
                    {
                        headers_checked = 1;
                    }
                }
            }
            else if (cache_file != NULL)
            {
                /*
                 * Headers have already been inspected.
                 * Save subsequent response bytes.
                 */
                if (fwrite(
                        buffer,
                        1,
                        (size_t)bytes_received,
                        cache_file) != (size_t)bytes_received)
                {
                    printf("Cache write failed.\n");

                    fclose(cache_file);
                    cache_file = NULL;

                    unlink(temp_filename);
                }
            }

            continue;
        }

        if (bytes_received == 0)
        {
            /*
             * The origin closed the connection.
             * Our request explicitly asked for
             * Connection: close.
             */
            response_complete = 1;
            break;
        }

        if (errno == EINTR)
            continue;

        perror("recv origin");
        break;
    }

    close(origin_fd);

    /* =========================================
       FINALIZE CACHE STORAGE
       ========================================= */

    if (cache_file != NULL)
    {
        int close_result = fclose(cache_file);

        if (response_complete && close_result == 0)
        {
            /*
             * Publish the completed response atomically.
             * The temporary file is renamed only after
             * the origin connection completes.
             */
            if (rename(
                    temp_filename,
                    cache_filename) == 0)
            {
                printf(
                    "CACHE STORE: Response saved successfully.\n");
            }
            else
            {
                perror("Cache rename");
                unlink(temp_filename);
            }
        }
        else
        {
            printf(
                "CACHE STORE: Incomplete response. "
                "Removing temporary file.\n");

            unlink(temp_filename);
        }
    }
}

unsigned char *read_cache_file(const char *filename, size_t *length)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        return NULL;
    }

    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    if (file_size <= 0 || file_size > 10 * 1024 * 1024)
    {
        fclose(file);
        return NULL;
    }

    rewind(file);

    unsigned char *data = malloc((size_t)file_size);

    if (data == NULL)
    {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(data, 1, (size_t)file_size, file);

    fclose(file);

    if (bytes_read != (size_t)file_size)
    {
        free(data);
        return NULL;
    }

    *length = bytes_read;

    return data;
}
int cache_exists(const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        return 0;
    }

    fclose(file);

    return 1;
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
