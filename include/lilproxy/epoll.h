#ifndef LILPROXY_EPOLL_H
#define LILPROXY_EPOLL_H

#include <sys/epoll.h>

#include <lilproxy/types.h>

#define MAX_EVENTS 128
#define EP_BASE_EVENTS (EPOLLRDHUP | EPOLLHUP | EPOLLERR)

typedef struct _RELAY_CTX {
 int idx;
 int srcfd;
 int dstfd;
 endpoint_role role;
} relay_ctx;

int epoll_init();
void handle_events(int epfd, struct epoll_event *evts, int ne);

#endif
