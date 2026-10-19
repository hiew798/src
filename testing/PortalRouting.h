#pragma once
#include <string.h>

static const char PORTAL_URL[] =
    "https://192.168.4.1/";

inline bool isPortalHost(const char* host) {
    return strcmp(host, "192.168.4.1") == 0 ||
           strcmp(host, "192.168.4.1:443") == 0;
}

inline bool redirectToPortal(
    const char* method,
    const char* host,
    const char* path
) {
    if (strcmp(method, "GET") != 0 &&
        strcmp(method, "HEAD") != 0)
        return false;

    if (!isPortalHost(host))
        return true;

    if (strcmp(path, "/") == 0 ||
        strcmp(path, "/app.js") == 0 ||
        strcmp(path, "/style.css") == 0)
        return false;

    if (strncmp(path, "/api/", 5) == 0)
        return false;

    return true;
}