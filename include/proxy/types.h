#ifndef PROXY_TYPES_H
#define PROXY_TYPES_H

#define MAX_CLIENTS 100
#define ADDR_LEN 64
#define CLIENT_SENDER 0
#define TARGET_SENDER 1
#define MAX_PATTERN_LEN 256
#define MAX_RESPONSE_LEN 4096
#define MAX_READ_SIZE 4096
#define DEFAULT_INITIAL_CAP 20
#define ERROR_LEN 128

#define CONFIG_FILENAME "rules.txt"
#define MAX_FILENAME_LEN 128

#include <proxy/term.h>

#define ERR(...) fprintf(stderr, COLOR_ERROR "error: " RESET); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\r\n")
#define WARN(...) fprintf(stderr, COLOR_WARN "warn: " RESET); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\r\n")

#endif
