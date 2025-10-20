// http_client.c
// Simple HTTP POST Client for Health Reporting
// Sends JSON data to collector endpoint

#define MODULE_NAME "HTTP_CLIENT"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

// ============================================================================
// URL PARSING
// ============================================================================

/**
 * Parse URL into components
 * Supports: http://host:port/path or http://host/path (default port 80)
 */
typedef struct {
    char host[256];
    int port;
    char path[512];
} parsed_url_t;

static int parse_url(const char *url, parsed_url_t *parsed) {
    // Initialize defaults
    memset(parsed, 0, sizeof(parsed_url_t));
    parsed->port = 80;
    strcpy(parsed->path, "/");

    // Skip "http://" prefix if present
    const char *ptr = url;
    if (strncmp(ptr, "http://", 7) == 0) {
        ptr += 7;
    } else if (strncmp(ptr, "https://", 8) == 0) {
        LOG_ERROR("HTTPS not supported, use HTTP");
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

// ============================================================================
// HTTP POST IMPLEMENTATION
// ============================================================================

/**
 * Perform HTTP POST request with JSON data
 * @param url Full URL (e.g., "http://collector:5000/ingest")
 * @param json_data JSON payload to send
 * @param timeout_seconds Socket timeout
 * @return 0 on success (200 OK), -1 on failure
 */
int health_http_post_json(const char *url, const char *json_data, int timeout_seconds) {
    parsed_url_t parsed;
    if (parse_url(url, &parsed) != 0) {
        LOG_ERROR("Failed to parse collector URL: %s", url);
        return -1;
    }

    // Resolve hostname
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", parsed.port);

    LOG_DEBUG("Resolving %s:%s", parsed.host, port_str);

    int status = getaddrinfo(parsed.host, port_str, &hints, &res);
    if (status != 0) {
        LOG_ERROR("Failed to resolve %s: %s", parsed.host, gai_strerror(status));
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
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_WARN("Failed to set send timeout");
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_WARN("Failed to set receive timeout");
    }

    // Connect
    LOG_DEBUG("Connecting to %s:%d", parsed.host, parsed.port);

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        LOG_ERROR("Failed to connect to %s:%d - %s",
                  parsed.host, parsed.port, strerror(errno));
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    // Build HTTP POST request
    size_t content_length = strlen(json_data);

    // Allocate HTTP request buffer on heap (8KB is too large for stack)
    char *http_request = malloc(8192);
    if (!http_request) {
        LOG_ERROR("Failed to allocate memory for HTTP request buffer");
        close(sockfd);
        return -1;
    }

    int request_len = snprintf(http_request, 8192,
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "User-Agent: AREDN-Phonebook-Health/1.0\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        parsed.path,
        parsed.host,
        parsed.port,
        content_length,
        json_data);

    if (request_len >= 8192) {
        LOG_ERROR("HTTP request too large");
        free(http_request);
        close(sockfd);
        return -1;
    }

    // Send request
    LOG_DEBUG("Sending HTTP POST (%d bytes)", request_len);

    ssize_t sent = send(sockfd, http_request, request_len, 0);
    if (sent < 0) {
        LOG_ERROR("Failed to send HTTP request: %s", strerror(errno));
        free(http_request);
        close(sockfd);
        return -1;
    }

    if (sent != request_len) {
        LOG_ERROR("Incomplete send: %zd of %d bytes", sent, request_len);
        free(http_request);
        close(sockfd);
        return -1;
    }

    // Request sent successfully, can free now
    free(http_request);

    // Allocate response buffer on heap (4KB is too large for stack)
    char *response = malloc(4096);
    if (!response) {
        LOG_ERROR("Failed to allocate memory for HTTP response buffer");
        close(sockfd);
        return -1;
    }

    ssize_t received = recv(sockfd, response, 4096 - 1, 0);
    close(sockfd);

    if (received < 0) {
        LOG_ERROR("Failed to receive HTTP response: %s", strerror(errno));
        free(response);
        return -1;
    }

    response[received] = '\0';

    LOG_DEBUG("Received HTTP response (%zd bytes)", received);

    // Parse status code
    // Format: HTTP/1.1 200 OK
    if (received < 12) {
        LOG_ERROR("HTTP response too short");
        free(response);
        return -1;
    }

    int status_code = 0;
    if (sscanf(response, "HTTP/1.%*d %d", &status_code) != 1) {
        LOG_ERROR("Failed to parse HTTP status code");
        free(response);
        return -1;
    }

    if (status_code == 200) {
        LOG_DEBUG("HTTP POST successful (200 OK)");
        free(response);
        return 0;
    } else {
        LOG_WARN("HTTP POST failed with status %d", status_code);
        free(response);
        return -1;
    }
}

/**
 * HTTP POST with retry
 * Attempts POST, retries once on failure
 * @param url Collector URL
 * @param json_data JSON payload
 * @param timeout_seconds Timeout per attempt
 * @return 0 on success, -1 on failure
 */
int health_http_post_with_retry(const char *url, const char *json_data, int timeout_seconds) {
    int result = health_http_post_json(url, json_data, timeout_seconds);

    if (result != 0) {
        LOG_WARN("First POST attempt failed, retrying in 2 seconds...");
        sleep(2);
        result = health_http_post_json(url, json_data, timeout_seconds);

        if (result != 0) {
            LOG_ERROR("Second POST attempt failed, giving up");
        }
    }

    return result;
}
