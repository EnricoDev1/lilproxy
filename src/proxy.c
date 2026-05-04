#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <lilproxy/net.h>
#include <lilproxy/proxy.h>
#include <lilproxy/rules.h>
#include <lilproxy/types.h>

proxyContext *ctx;
proxyTarget *target;
blacklist *bl;
appConfig *cfg;

/* Allocate the proxy context struct with error handling */ 
static proxyContext *ctx_malloc() {
  proxyContext *ctx = malloc(sizeof(*ctx));
  if (ctx == NULL) {
    perror("malloc ctx");
    return NULL;
  }
  return ctx;
}

appConfig *cfg_init() {
  appConfig *cfg = malloc(sizeof(*cfg));

  if (cfg == NULL) {
    perror("malloc cfg");
    return NULL;
  }

  cfg->rules_file = malloc(MAX_FILENAME_LEN * sizeof(char));
  if (cfg->rules_file == NULL) {
    perror("malloc cfg->rules_file");
    return NULL;
  }
  return cfg;
}

/* Initialize the proxy context to default values. */
int connection_init() {
  ctx = ctx_malloc();
  if (ctx == NULL) return -1;
  memset(ctx, 0, sizeof(*ctx));
  
  /* we don't have any client connected yet */
  ctx->numclients = 0;
  ctx->serversock = server_init(cfg->l_port); 
  
  if (ctx->serversock == -1) {
    perror("Creating listening socket");
    return -1;
  }
  return 0;
}

/* Initialize proxy state with values received from command line. */
void proxy_init() {
  target = malloc(sizeof(*target));
  memset(target, 0, sizeof(*target));
  snprintf(target->addr, ADDR_LEN, "%s", cfg->addr);
  target->port = cfg->t_port;
}

/*
  Send data received from src to dst. It handles partial writes by looping untile all bytes have been sent. 
  Returns -1 on disconnections/errors, 0 otherwise.
*/
int relay(int src, int dst, int sender) {
  unsigned char buf[MAX_READ_SIZE];
  int n = read(src, buf, sizeof(buf)-1);
  
  if (n <= 0) return -1;
      
  rule *r = check_rules(buf, n);

  /* block patterns only if they come from client */
  if (r != NULL && sender == EP_CLIENT) {
    switch (r->action) {
      case ACTION_BLOCK:
        return 0;
      case ACTION_DROP:
        return -1;
      case ACTION_REPLY: {
        char err[ERROR_LEN];
        if(rule_reply_refresh(r, err) == -1) {
          ERR("reply file error (id=%d): %s", r->id, err);
          return 0; 
        } 
        write(src, r->response, r->r_len);
        return 0;
      }
      case ACTION_NONE:
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

