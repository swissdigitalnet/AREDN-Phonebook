// uac_http_client.h
// Simple HTTP GET Client for Location Data Fetching
// Fetches sysinfo.json from AREDN nodes to get lat/lon coordinates

#ifndef UAC_HTTP_CLIENT_H
#define UAC_HTTP_CLIENT_H

#include <stddef.h>

/**
 * Fetch location data from sysinfo.json endpoint
 *
 * Performs HTTP GET to http://<node_ip>/cgi-bin/sysinfo.json
 * and extracts "lat" and "lon" fields from JSON response.
 *
 * @param url Full URL to sysinfo.json (e.g., "http://10.51.55.1/cgi-bin/sysinfo.json")
 * @param lat Output buffer for latitude string
 * @param lat_len Size of latitude buffer
 * @param lon Output buffer for longitude string
 * @param lon_len Size of longitude buffer
 * @return 0 on success, -1 on error
 */
int uac_http_get_location(
    const char *url,
    char *lat,
    size_t lat_len,
    char *lon,
    size_t lon_len
);

#endif // UAC_HTTP_CLIENT_H
