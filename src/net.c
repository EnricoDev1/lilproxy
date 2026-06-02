#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <lilproxy/net.h>
#include <lilproxy/state.h>

int bind_and_listen(struct sockaddr *sockaddr, socklen_t len, int fd) {
  if (bind(fd, sockaddr, len) == -1) {
    ERR("unable to bind socket (fd = %d)", fd);
    close(fd);
    return -1;
  }

  if (listen(fd, 3) == -1) {
    ERR("unable to listen (fd = %d)", fd);
    close(fd);
    return -1;
  }
  return 0;
}

/* Init the TCP connection and returns the assigned server socket file descriptor. */
int server_init() {
  struct sockaddr_in sa;
  int optval = 1;
  
  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) return -1;
  
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  memset(&sa, 0, sizeof(sa));
  
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons(cfg->l_port);

  if (bind_and_listen((struct sockaddr *)&sa, sizeof(sa), sfd) < 0) return -1;
    
  return sfd;
}

static char *setup_runtime_path() {
  char *runtime_path = malloc(255);
  char *runtime_dir = getenv("XDG_RUNTIME_DIR");

  if (runtime_dir == NULL) {
    struct stat st = {0};
    sprintf(runtime_path, "%s", "/run/lilproxy/lilproxy.sock");
    if (stat("/run/lilproxy", &st) == -1) {
      mkdir("/run/lilproxy", 0600);
    }
  } else {
    sprintf(runtime_path, "%s/lilproxy.sock", runtime_dir);
  }

  return runtime_path;
}

/* Init the UNIX socket server and return the assigned file descriptor */
int command_sock_init() {
  int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sfd < 0) {
    perror("socket");
    return -1;
  }

  struct sockaddr_un sa;
  memset(&sa, 0, sizeof(sa));
  
  sa.sun_family = AF_UNIX;  
  char *runtime_path = setup_runtime_path();
  sprintf(sa.sun_path, "%s", runtime_path);
  
  if (unlink(runtime_path) == -1 && errno != ENOENT) {
    ERR("unable to unlink old command socket");
    close(sfd);
    return -1;
  }
  
  if (bind_and_listen((struct sockaddr *)&sa, sizeof(sa), sfd)) return -1;
    
  if (chmod(runtime_path, 0600) == -1) {
    ERR("failed to set permissions on %s", runtime_path);
    return -1;
  }
  
  return sfd;
}

/*
  This function stops read/write/connect from being blocking: if there's not ready data they set errno = EAGAIN.
  It also prevents TCP from "packing" data, reduce delay. 
*/
static int socket_set_non_block(int fd) {
  int flags;
  int y = 1;

  if ((flags = fcntl(fd, F_GETFL)) == -1) return -1;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;

  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &y, sizeof(y));
  return 0;
}

/*
  Connect using TCP stream to a remote target. This function supports both ipv4 and ipv6.
  If connect() returns EINPROGRESS, the socket is returned to the caller, which
  will be responsible for waiting (using epoll) until the connection is successfully enstablished.
  
  Returns -1 on error, socket file descriptor used for connection to the remote server on success.
*/
static int tcp_conn_start(const char *addr, int port, int *status) {
  struct addrinfo hints, *servinfo, *p;
  char portstr[6];
  int fd = -1;

  snprintf(portstr, sizeof(portstr), "%d", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  /* ipv4 and ipv6 support, using a list of addrinfo */
  if (getaddrinfo(addr, portstr, &hints, &servinfo) != 0) return -1;

  for (p = servinfo; p != NULL; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd == -1) continue;
    
    if (socket_set_non_block(fd) == -1) { close(fd); continue; }

    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      *status = CONNECTED;
      freeaddrinfo(servinfo);
      return fd;      
    }

    if (errno == EINPROGRESS) {
      *status = CONNECTING;
      freeaddrinfo(servinfo);
      return fd;
    }

    close(fd);
    fd = -1;
  }

  freeaddrinfo(servinfo);
  return -1;
}

int check_target_connect(int fd) {
  int err = 0;
  socklen_t len = sizeof(err);

  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1) return -1;
  if (err != 0) {errno = err; return -1;}

  return 0;
}

/* This function accepts a new client from the UNIX command socket, returning the associated file descriptor. */
int accept_command_client() {
  int cfd;
  struct sockaddr_un sa;
  socklen_t slen = sizeof(sa);

  cfd = accept(ctx->commandsock, (struct sockaddr*)&sa, &slen);

  if (cfd < 0) {
    ERR("unable to accept command client");
    return -1;
  }
  
  return cfd;
}

/*
  Accept a client and open the matching target connection. The caller owns
  registering and tracking both fds.
  
  It returns 0 on success, -1 on error.
*/
int accept_proxy_pair(int *client_fd, int *target_fd, target_status *status) {
  int cfd;
  struct sockaddr_in sa;
  socklen_t slen = sizeof(sa);

  if (client_fd == NULL || target_fd == NULL || status == NULL) return -1;

  cfd = accept(ctx->serversock, (struct sockaddr*)&sa, &slen);
  if (cfd < 0) {
    ERR("unable to accept client");
    return -1;
  }

  if (ctx->numclients >= MAX_CLIENTS) {
    close(cfd);
    errno = EMFILE;
    return -1;
  }

  if (socket_set_non_block(cfd) == -1) {
    close(cfd);
    return -1;
  }

  // connect to the target and check if the operation is still in progress
  int conn_status = -1;
  int tfd = tcp_conn_start(target->addr, target->port, &conn_status);
  if (tfd == -1) {
    ERR("connection to target failed");
    close(cfd);
    return -1;
  }

  *client_fd = cfd;
  *target_fd = tfd;
  *status = conn_status;
  return 0;
}
