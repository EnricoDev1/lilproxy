#ifndef PROXY_NET_H
#define PROXY_NET_H

#include <sys/select.h>

typedef enum _TARGET_STATUS {
  CONNECTED = 0,
  CONNECTING  
} target_status;

int server_init(int port);
int accept_client();
int build_fd_set(fd_set *readfds);
void close_session(int idx);
int target_conn_finish(int fd);

#endif
