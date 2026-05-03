#include <stdint.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include <proxy/args.h>
#include <proxy/state.h>
#include <proxy/commands.h>
#include <proxy/epoll.h>

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
    ERR("cannot enable raw_mode");
    return -1;
  }

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
  
  /* disable raw mode at exit */
  set_raw_mode(STDIN_FILENO, 0);
  return 0;
}
