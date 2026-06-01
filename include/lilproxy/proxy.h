#ifndef LILPROXY_PROXY_H
#define LILPROXY_PROXY_H

#include "types.h"

typedef struct _APP_CONFIG {
  char *rules_file;
  int l_port; // listen port
} appConfig;

typedef struct _LILPROXY_CONTENT {
  int serversock;
  int commandsock;
  int numclients;
} proxyContext;

typedef struct _LILPROXY_TARGET {
  char addr[ADDR_LEN];
  int port;
} proxyTarget;


appConfig *new_config();
int init_listeners();
int init_runtime();
int setup_app(int argc, char *argv[]);
int relay_once(int src, int dst, endpoint_role sender);

#endif
