// http_client.c
// Simple HTTP GET Client Implementation

#define MODULE_NAME "HTTP_CLIENT"

#include "http_client.h"
#include "../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

// HTTP timeout
#define HTTP_TIMEOUT_SEC 2

/**
 * Parse URL into components
 */
typedef struct {
    char host[256];
    int port;
    char path[512];
} ParsedURL;

static int parse_url(const char *url, ParsedURL *parsed) {
    if (!url || !parsed) {
        return -1;
    }

    // Initialize defaults
    memset(parsed, 0, sizeof(ParsedURL));
    parsed->port = 80;
    strcpy(parsed->path, "/");

    // Skip "http://" prefix if present
    const char *ptr = url;
    if (strncmp(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (strncmp(ptr, "https://", 8) == 0) {
        LOG_ERROR("HTTPS not supported");
        return -1;
    }

    // Find path separator
    const char *path_start = strchr(ptr, '/');

    // Find port separator
    const char *port_start = strchr(ptr, ':');

    // Extract host
    int host_len = 0;
    if (port_start && (!path_start || port_start < path_start)) {
        // Port specified
        host_len = port_start - ptr;
        if (host_len >= sizeof(parsed->host)) {
            LOG_ERROR("Hostname too long");
            return -1;
        }
        strncpy(parsed->host, ptr, host_len);
        parsed->host[host_len] = '\0';

        // Extract port
        parsed->port = atoi(port_start + 1);
        if (parsed->port <= 0 || parsed->port > 65535) {
            LOG_ERROR("Invalid port number");
            return -1;
        }
    } else if (path_start) {
        // No port, but path exists
        host_len = path_start - ptr;
        if (host_len >= sizeof(parsed->host)) {
            LOG_ERROR("Hostname too long");
            return -1;
        }
        strncpy(parsed->host, ptr, host_len);
        parsed->host[host_len] = '\0';
    } else {
        // Just hostname
        strncpy(parsed->host, ptr, sizeof(parsed->host) - 1);
        parsed->host[sizeof(parsed->host) - 1] = '\0';
    }

    // Extract path
    if (path_start) {
        strncpy(parsed->path, path_start, sizeof(parsed->path) - 1);
        parsed->path[sizeof(parsed->path) - 1] = '\0';
    }

    LOG_DEBUG("Parsed URL: host=%s port=%d path=%s",
              parsed->host, parsed->port, parsed->path);

    return 0;
}

/**
 * Extract JSON string value by key
 * Simple implementation - looks for "key": "value" pattern (handles optional whitespace)
 */
static int extract_json_string(const char *json, const char *key, char *value, size_t value_len) {
    if (!json || !key || !value) {
        return -1;
    }

    // Build search pattern: "key"
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *start = strstr(json, pattern);
    if (!start) {
        LOG_DEBUG("JSON key '%s' not found", key);
        return -1;
    }

    // Move past the key
    start += strlen(pattern);

    // Skip optional whitespace and colon
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    if (*start != ':') {
        LOG_DEBUG("Expected ':' after key '%s'", key);
        return -1;
    }
    start++; // Skip colon

    // Skip optional whitespace after colon
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }

    // Expect opening quote
    if (*start != '"') {
        LOG_DEBUG("Expected '\"' for value of key '%s'", key);
        return -1;
    }
    start++; // Skip opening quote

    // Find closing quote
    const char *end = strchr(start, '"');
    if (!end) {
        LOG_DEBUG("Malformed JSON value for key '%s'", key);
        return -1;
    }

    // Copy value
    size_t len = end - start;
    if (len >= value_len) {
        len = value_len - 1;
    }
    strncpy(value, start, len);
    value[len] = '\0';

    LOG_DEBUG("Extracted JSON: %s = %s", key, value);
    return 0;
}

/**
 * Perform HTTP GET request
 */
static int http_get(const char *url, char *response, size_t response_len) {
    ParsedURL parsed;
    if (parse_url(url, &parsed) != 0) {
        LOG_ERROR("Failed to parse URL: %s", url);
        return -1;
    }

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", parsed.port);

    int status = getaddrinfo(parsed.host, port_str, &hints, &res);
    if (status != 0) {
        LOG_DEBUG("Failed to resolve %s: %s", parsed.host, gai_strerror(status));
        return -1;
    }

    // Create socket
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    // Set socket timeouts
    struct timeval tv;
    tv.tv_sec = HTTP_TIMEOUT_SEC;
    tv.tv_usec = 0;

    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Connect
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        LOG_DEBUG("Failed to connect to %s:%d - %s",
                  parsed.host, parsed.port, strerror(errno));
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    // Build HTTP GET request
    char request[1024];
    int request_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: AREDN-Phonebook/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path,
        parsed.host,
        parsed.port);

    if (request_len >= sizeof(request)) {
        LOG_ERROR("HTTP request too large");
        close(sockfd);
        return -1;
    }

    // Send request
    ssize_t sent = send(sockfd, request, request_len, 0);
    if (sent < 0) {
        LOG_DEBUG("Failed to send HTTP request: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Receive response
    size_t received_total = 0;
    while (received_total < response_len - 1) {
        ssize_t n = recv(sockfd, response + received_total,
                        response_len - received_total - 1, 0);
        if (n <= 0) {
            break;
        }
        received_total += n;
    }
    response[received_total] = '\0';

    close(sockfd);

    if (received_total == 0) {
        LOG_DEBUG("No response received from %s", url);
        return -1;
    }

    // Find JSON body (skip HTTP headers)
    char *json_start = strstr(response, "\r\n\r\n");
    if (json_start) {
        json_start += 4;
        memmove(response, json_start, strlen(json_start) + 1);
    }

    LOG_DEBUG("HTTP GET successful: %s (%zu bytes)", url, strlen(response));
    return 0;
}

/**
 * Fetch location data from sysinfo.json
 */
int http_get_location(const char *url, char *lat, size_t lat_len,
                          char *lon, size_t lon_len) {
    if (!url || !lat || !lon) {
        return -1;
    }

    // Fetch sysinfo.json
    char response[4096];
    if (http_get(url, response, sizeof(response)) != 0) {
        return -1;
    }

    // Extract lat and lon from JSON
    if (extract_json_string(response, "lat", lat, lat_len) != 0) {
        return -1;
    }

    if (extract_json_string(response, "lon", lon, lon_len) != 0) {
        return -1;
    }

    LOG_DEBUG("Fetched location from %s: lat=%s, lon=%s", url, lat, lon);
    return 0;
}
