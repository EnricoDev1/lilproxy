#ifndef PROXY_PROXY_H
#define PROXY_PROXY_H

#include "types.h"

typedef struct _APP_CONFIG {
  char *rules_file;
  int l_port; // listen port
  int c_port; // command port
} appConfig;

typedef struct _PROXY_CONTENT {
  int serversock;
  int commandsock;
  int numclients;
  int targets[MAX_CLIENTS];
  int clients[MAX_CLIENTS];
  int t_status[MAX_CLIENTS];
} proxyContext;

typedef struct _PROXY_TARGET {
  char addr[ADDR_LEN];
  int port;
} proxyTarget;


appConfig *new_config();
int init_listeners();
int init_runtime();
int setup_app(int argc, char *argv[]);
int relay_once(int src, int dst, int sender);

#endif
