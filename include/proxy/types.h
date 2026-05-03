#ifndef PROXY_TYPES_H
#define PROXY_TYPES_H

#include <stdio.h>
#include <stdarg.h>

#define MAX_CLIENTS 1000
#define ADDR_LEN 64
#define MAX_PATTERN_LEN 256
#define MAX_RESPONSE_LEN 4096
#define MAX_READ_SIZE 10000
#define DEFAULT_INITIAL_CAP 20
#define ERROR_LEN 128

#define CONFIG_FILENAME "rules.txt"
#define MAX_FILENAME_LEN 128

#include <proxy/term.h>

typedef enum _ENDPOINT_ROLE {
  EP_CLIENT,
  EP_TARGET
} endpoint_role;

static inline void ERR(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, COLOR_ERROR "error: " RESET);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\r\n");

    va_end(args);
}

static inline void WARN(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, COLOR_WARN "warn: " RESET);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\r\n");

    va_end(args);
}

#endif
