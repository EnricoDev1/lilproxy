#ifndef PROXY_PROXY_H
#define PROXY_PROXY_H

#include "types.h"

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

appConfig *cfg_init();
void connection_init();
void proxy_init();
int relay(int src, int dst, int sender);

#endif
