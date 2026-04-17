#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "args.h"
#include "net.h"
#include "proxy.h"
#include "rules.h"
#include "state.h"
#include "commands.h"

int main(int argc, char *argv[]) {
  cfg = cfg_init();
  parse_args(argc, argv);
    
  if (cfg->l_port == 0) {
    fprintf(stderr, "Usage: %s -l <local-port> -a <target-addr> -t <target-port> [-r <config-file>]", argv[0]);
    exit(1);
  }

  connection_init();
  proxy_init();
  load_rules();
  if (set_raw_mode(STDIN_FILENO, 1) == -1) {
    perror("set_raw_mode");
    exit(1);
  }
  struct timeval tv;
  fd_set readfds;
  
  while (1) {
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int maxfd = build_fd_set(&readfds);
    int retval = select(maxfd+1, &readfds, NULL, NULL, &tv);
    
    if (retval == -1) {
      perror("select() error");
      exit(1);
    }

    /* One or more file descriptors are ready. */
    if (retval) {
      /* A new client wants to connect. */
      if (FD_ISSET(ctx->serversock, &readfds)) {
        accept_client();
      }

      /* We received something from stdin */
      if (FD_ISSET(STDIN_FILENO, &readfds)) {
        read_command();
      }

      /* For each active session, relay data in both directions. */
      for (int i = 0; i < ctx->numclients; i++) {
        if (ctx->clients[i] && FD_ISSET(ctx->clients[i], &readfds)) {
          if (relay(ctx->clients[i], ctx->targets[i], CLIENT_SENDER) == -1) {
            close_session(i--);
            continue;
          }
        }
        if (i >= 0 && ctx->targets[i] > 0 && FD_ISSET(ctx->targets[i], &readfds)) {
          if (relay(ctx->targets[i], ctx->clients[i], TARGET_SENDER) == -1) {
            close_session(i--);
          }
        }
      }      
    } else {
      /* Timeout reached... */
    }
  }  
  return 0;
}
