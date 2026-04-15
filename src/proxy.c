#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "net.h"
#include "proxy.h"
#include "rules.h"

proxyContext *ctx;
proxyTarget *target;
blacklist *bl;

/* Allocate the proxy context struct with error handling */ 
void *ctx_malloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    perror("Memory error while malloc");
    exit(1);
  }
  return ptr;
}

/* Initialize the proxy context to default values. */
void connection_init(int port) {
  ctx = ctx_malloc(sizeof(*ctx));
  memset(ctx, 0, sizeof(*ctx));
  
  /* we don't have any client connected yet */
  ctx->numclients = 0;
  ctx->maxclient = -1; 
  ctx->serversock = server_init(port); 
  
  if (ctx->serversock == -1) {
    perror("Creating listening socket");
    exit(1);
  }
}

/* Initialize proxy state with values received from command line. */
void proxy_init(const char *addr, int port) {
  target = malloc(sizeof(*target));
  memset(target, 0, sizeof(*target));
  snprintf(target->addr, ADDR_LEN, "%s", addr);
  target->port = port;
}

/*
  Send data received from src to dst. It handles partial writes by looping untile all bytes have been sent. 
  Returns -1 on disconnections/errors, 0 otherwise.

  NOTE: int sender it isn't used at the moment, but it could be useful in future.
*/
int relay(int src, int dst, int sender) {
  unsigned char buf[MAX_READ_SIZE];
  int n = read(src, buf, sizeof(buf)-1);
  
  if (n <= 0) return -1;
    
  buf[n] = '\0';
  
  rule *r = check_rules(buf, n);  
  if (r != NULL) {
    switch (r->action) {
      case ACTION_BLOCK:
        return 0;
      case ACTION_DROP:
        return -1;
      case ACTION_REPLY:
        write(src, r->response, r->r_len);
        return 0;
    }
  }

  int wrtn = 0;
  while (wrtn < n) {
    int w = write(dst, buf + wrtn, n - wrtn);
    if (w <= 0) return -1;
    wrtn += w;
  }
  
  return 0;
}
