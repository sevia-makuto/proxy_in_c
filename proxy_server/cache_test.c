#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/sha.h>
#include <time.h>
#include <sys/stat.h>

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
        sprintf(&hex_hash[i * 2], "%02x", hash[i]);
    }

    snprintf(filename, 256, "cache/%s.cache", hex_hash);
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
int save_to_cache(const char *filename,
                  const unsigned char *response,
                  size_t response_length)
{
    FILE *file = fopen(filename, "wb");

    if (file == NULL)
    {
        perror("Unable to open cache file");
        return 0;
    }

    size_t bytes_written = fwrite(
        response,
        1,
        response_length,
        file);

    fclose(file);

    if (bytes_written != response_length)
    {
        fprintf(stderr, "Failed to write complete response.\n");
        return 0;
    }

    return 1;
}
unsigned char *read_from_cache(
    const char *filename,
    size_t *response_length)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        return NULL;
    }

    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    if (file_size < 0)
    {
        fclose(file);
        return NULL;
    }

    rewind(file);

    unsigned char *response = malloc((size_t)file_size);

    if (response == NULL && file_size > 0)
    {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(
        response,
        1,
        (size_t)file_size,
        file);

    fclose(file);

    if (bytes_read != (size_t)file_size)
    {
        free(response);
        return NULL;
    }

    *response_length = bytes_read;

    return response;
}
int cache_is_fresh(const char *filename, int ttl)
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

    // Temporary debugging statements
    printf("Elapsed time: %.0f seconds\n", elapsed);
    printf("TTL: %d seconds\n", ttl);

    if (elapsed < ttl)
    {
        return 1;
    }

    return 0;
}
int extract_max_age(const char *headers)
{
    const char *cache_control = strstr(headers, "Cache-Control:");

    if (cache_control == NULL)
    {
        return -1;
    }

    const char *line_end = strstr(cache_control, "\r\n");

    if (line_end == NULL)
    {
        return -1;
    }

    const char *max_age = strstr(cache_control, "max-age=");

    if (max_age == NULL || max_age >= line_end)
    {
        return -1;
    }

    max_age += strlen("max-age=");

    return atoi(max_age);
}
int has_no_store(const char *headers)
{
    const char *cache_control =
        strstr(headers, "Cache-Control:");

    if (cache_control == NULL)
    {
        return 0;
    }

    const char *line_end = strstr(cache_control, "\r\n");

    if (line_end == NULL)
    {
        return 0;
    }

    const char *no_store = strstr(cache_control, "no-store");

    if (no_store == NULL || no_store >= line_end)
    {
        return 0;
    }

    return 1;
}
int should_cache_response(const char *headers, int *ttl)
{
    int status_code = 0;

    // Check HTTP status code
    if (sscanf(headers, "HTTP/%*u.%*u %d", &status_code) != 1)
    {
        return 0;
    }

    // Initially, cache only successful responses
    if (status_code != 200)
    {
        return 0;
    }

    // Never store no-store responses
    if (has_no_store(headers))
    {
        return 0;
    }

    // Extract freshness lifetime
    int max_age = extract_max_age(headers);

    if (max_age <= 0)
    {
        return 0;
    }

    *ttl = max_age;

    return 1;
}
int main(void)
{
    const char *url = "http://example.com/index.html";

    const unsigned char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 25\r\n"
        "\r\n"
        "Hello from example.com!";

    size_t response_length = sizeof(response) - 1;

    char filename[256];

    generate_cache_key(url, filename);

    printf("Original URL: %s\n", url);
    printf("Cache filename: %s\n", filename);

    if (cache_exists(filename))
    {
        printf("CACHE HIT: Resource exists in cache.\n");
    }
    else
    {
        printf("CACHE MISS: Resource not found in cache.\n");

        if (save_to_cache(filename, response, response_length))
        {
            printf("CACHE SAVE: Response saved successfully.\n");
        }
    }
    size_t cached_length = 0;

    unsigned char *cached_response =
        read_from_cache(filename, &cached_length);

    if (cached_response != NULL)
    {
        printf("\nRetrieved cached response:\n");

        fwrite(cached_response, 1, cached_length, stdout);

        free(cached_response);
    }
    else
    {
        printf("Unable to read cached response.\n");
    }
    int ttl = 10;

    if (cache_is_fresh(filename, ttl))
    {
        printf("CACHE STATUS: Fresh\n");
    }
    else
    {
        printf("CACHE STATUS: Stale or missing\n");
    }
    const char *test_headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n";

    if (has_no_store(test_headers))
    {
        printf("CACHE RULE: Do not store this response.\n");
    }
    else
    {
        printf("CACHE RULE: Response may be considered for caching.\n");
    }
    const char *response_headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Cache-Control: public, max-age=120\r\n"
        "\r\n";

    int response_ttl = 0;

    if (should_cache_response(response_headers, &response_ttl))
    {
        printf("Response is eligible for caching.\n");
        printf("TTL: %d seconds\n", response_ttl);
    }
    else
    {
        printf("Response must not be cached.\n");
    }

    return 0;
}