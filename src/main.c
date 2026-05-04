#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#include <lilproxy/args.h>
#include <lilproxy/state.h>
#include <lilproxy/commands.h>
#include <lilproxy/epoll.h>

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

  return 0;
}

int main(int argc, char *argv[]) {
  if (app_setup(argc, argv) == -1) return -1;
  
  int epfd = epoll_init();
  if (epfd == -1) return -1;
  
  struct epoll_event evts[MAX_EVENTS];
  
  for (;;) {    
    int ne = epoll_wait(epfd, evts, MAX_EVENTS, 1000);
    
    if (ne == -1) {
      if (errno == EINTR) continue;
      ERR("epoll_wait failed");
      break;
    }

    handle_events(epfd, evts, ne);
  }
  return 0;
}
