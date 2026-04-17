#ifndef PROXY_NET_H
#define PROXY_NET_H

#include "state.h"

int server_init(int port);
int accept_client();
int build_fd_set(fd_set *readfds);
void close_session(int idx);

#endif
