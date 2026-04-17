#ifndef PROXY_TYPES_H
#define PROXY_TYPES_H

#include <sys/select.h>

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

#define ERR(...) fprintf(stderr, __VA_ARGS__)

typedef struct _APP_CONFIG {
  char *rules_file;
  int l_port;
  int t_port;
  char addr[ADDR_LEN];
} appConfig;

typedef struct _PROXY_CONTENT {
  int serversock;
  int numclients;
  int maxclient;
  int targets[MAX_CLIENTS];
  int clients[MAX_CLIENTS];
} proxyContext;

typedef struct _PROXY_TARGET {
  char addr[ADDR_LEN];
  int port;
} proxyTarget;

typedef enum _RULE_ACTION {
  ACTION_BLOCK,
  ACTION_REPLY,
  ACTION_DROP,
  ACTION_NONE
} rule_action;

typedef struct _RULE {
  int len;
  int r_len;
  rule_action action;
  char *pattern;
  char *response;
} rule;

typedef struct _BLACKLIST {
  rule **rules;
  int nrules;
  int capacity;
} blacklist;

#endif
