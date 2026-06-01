#ifndef LILPROXY_EPOLL_H
#define LILPROXY_EPOLL_H

#include <sys/epoll.h>

#include <lilproxy/types.h>

#define MAX_EVENTS 128
#define EP_BASE_EVENTS (EPOLLRDHUP | EPOLLHUP | EPOLLERR)

typedef enum _FD_KIND {
  FD_UNUSED = 0,
  FD_LISTENER_TCP,
  FD_LISTENER_CMD,
  FD_STDIN,
  FD_PROXY_CLIENT,
  FD_PROXY_TARGET,
  FD_CMD_CLIENT
} fd_kind;

int epoll_init();
void handle_events(int epfd, struct epoll_event *evts, int ne);

#endif
