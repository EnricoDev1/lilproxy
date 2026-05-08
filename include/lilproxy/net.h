#ifndef LILPROXY_NET_H
#define LILPROXY_NET_H

typedef enum _TARGET_STATUS {
  CONNECTED = 0,
  CONNECTING  
} target_status;

int server_init(int port);
int accept_client();
void close_session(int idx);
int target_conn_finish(int fd);

#endif
