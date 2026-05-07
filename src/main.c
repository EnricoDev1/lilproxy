#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#include <lilproxy/state.h>
#include <lilproxy/epoll.h>

int main(int argc, char *argv[]) {
  if (setup_app(argc, argv) == -1) return -1;
  
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
