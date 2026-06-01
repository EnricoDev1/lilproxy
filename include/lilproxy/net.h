#ifndef LILPROXY_NET_H
#define LILPROXY_NET_H

typedef enum _TARGET_STATUS {
  CONNECTED = 0,
  CONNECTING
} target_status;

int server_init();
int accept_proxy_pair(int *client_fd, int *target_fd, target_status *status);
int accept_command_client();
int check_target_connect(int fd);
int command_sock_init();

#endif
