#include "proxy/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <proxy/args.h>
#include <proxy/net.h>
#include <proxy/proxy.h>
#include <proxy/rules.h>
#include <proxy/state.h>
#include <proxy/commands.h>

int app_setup(int argc, char *argv[]) {
  cfg = cfg_init();
  if (cfg == NULL) return -1;
  
  if (parse_args(argc, argv) == -1) return -1;
    
  if (cfg->l_port == 0) {
    ERR("Usage: %s -l <local-port> -a <target-addr> -t <target-port> [-r <rules-file>]", argv[0]);
    return -1;
  }

  if (connection_init() == -1) return -1;
  proxy_init();
  load_rules();
  commands_init();

  if (set_raw_mode(STDIN_FILENO, 1) == -1) {
    perror("set_raw_mode");
    return -1;
  }

  return 0;
}

void handle_sessions(fd_set *readfds) {
  for (int i = 0; i < ctx->numclients; i++) {
    int client_fd = ctx->clients[i];
    int target_fd = ctx->targets[i];

    if (client_fd > 0 && FD_ISSET(client_fd, readfds)) {
      if (relay(client_fd, target_fd, CLIENT_SENDER) == -1) {
        close_session(i--);
        continue;
      }
    }

    if (i >= 0 && target_fd > 0 && FD_ISSET(target_fd, readfds)) {
      if (relay(target_fd, client_fd, TARGET_SENDER) == -1) {
        close_session(i--);
        continue;
      }
    }
  }
}

void handle_events(fd_set *readfds) {
  if (FD_ISSET(ctx->serversock, readfds)) accept_client();
  if (FD_ISSET(STDIN_FILENO, readfds)) read_command();
  handle_sessions(readfds);
}

int main(int argc, char *argv[]) {
  if (app_setup(argc, argv) == -1) return -1;
  
  struct timeval tv;
  fd_set readfds;
  
  while (1) {
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int maxfd = build_fd_set(&readfds);
    int retval = select(maxfd+1, &readfds, NULL, NULL, &tv);
    
    if (retval == -1) {
      perror("select() error");
      return 1;
    }

    if (retval > 0) {
      handle_events(&readfds);
    }
  }

  /* disable raw mode at exit */
  set_raw_mode(STDIN_FILENO, 0);
  return 0;
}
