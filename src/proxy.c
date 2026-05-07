#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <lilproxy/net.h>
#include <lilproxy/proxy.h>
#include <lilproxy/rules.h>
#include <lilproxy/types.h>
#include <lilproxy/args.h>
#include <lilproxy/commands.h>

proxyContext *ctx;
proxyTarget *target;
blacklist *bl;
appConfig *cfg;

/* Allocate the proxy context struct with error handling */ 
static proxyContext *new_context() {
  proxyContext *ctx = malloc(sizeof(*ctx));
  if (ctx == NULL) {
    perror("malloc ctx");
    return NULL;
  }
  return ctx;
}

appConfig *new_config() {
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
int init_listeners() {
  ctx->numclients = 0;  
  ctx->serversock = server_init(cfg->l_port); 
  ctx->commandsock = server_init(cfg->c_port);

  printf("%d-%d\r\n", cfg->c_port, ctx->commandsock);
  
  if (ctx->serversock == -1) {
    ERR("unable to create server socket");
    return -1;
  }

  if (ctx->commandsock == -1) {
    ERR("unable to create server config socket");
    return -1;
  }
  
  return 0;
}

/* allocate config related structs */
int init_runtime() {
  cfg = new_config();
  if (cfg == NULL) {ERR("unable to init lilproxy config"); return -1;}
  
  ctx = new_context();
  if (ctx == NULL) {ERR("unable to malloc lilproxy ctx"); return -1;}

  target = malloc(sizeof(*target));
  if (target == NULL) {ERR("unable to malloc target"); return -1;}

  memset(target, 0, sizeof(*target));
  memset(ctx, 0, sizeof(*ctx));
  return 0;
}

/* Initialize all the proxy configs, using helper functions. */
int setup_app(int argc, char *argv[]) {
  if (init_runtime() == -1) return -1;  
  if (parse_args(argc, argv) == -1) return -1;
    
  if (cfg->l_port == 0 || target->port == 0) {
    ERR("Usage: %s -l <local-port> -a <target-addr> -t <target-port> [-r <rules-file>]", argv[0]);
    return -1;
  }
  if (init_listeners() == -1) return -1;
  load_rules();
  commands_init();

  return 0;
}

/*
  Send data received from src to dst. It handles partial writes by looping untile all bytes have been sent. 
  Returns -1 on disconnections/errors, 0 otherwise.
*/
int relay_once(int src, int dst, int sender) {
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
