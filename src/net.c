#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <lilproxy/net.h>
#include <lilproxy/state.h>

/* Init the TCP connection and returns the assigned server socket file descriptor. */
int server_init(int port) {
  struct sockaddr_in sa;
  int sfd, optval = 1;
  
  if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  memset(&sa, 0, sizeof(sa));
  
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons(port);

  if (bind(sfd, (struct sockaddr*)&sa, sizeof(sa)) == -1 || listen(sfd, 3)) {
    close(sfd);
    return -1;
  }
  
  return sfd;
}

/*
  This function stops read/write/connect from being blocking: if there's not ready data they set errno = EAGAIN.
  It also prevents TCP from "packing" data, reduce delay. 
*/
static int socketSetNonBlockNoDelay(int fd) {
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
static int tcp_conn_start(const char *addr, int port, int *is_connecting) {
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
    
    if (socketSetNonBlockNoDelay(fd) == -1) { close(fd); continue; }

    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      *is_connecting = CONNECTED;
      freeaddrinfo(servinfo);
      return fd;      
    }

    if (errno == EINPROGRESS) {
      *is_connecting = CONNECTING;
      freeaddrinfo(servinfo);
      return fd;
    }

    close(fd);
    fd = -1;
  }

  freeaddrinfo(servinfo);
  return -1;
}

/* returns 0 if  */
int target_conn_finish(int fd) {
  int err = 0;
  socklen_t len = sizeof(err);
  int flags;

  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1) return -1;
  if (err != 0) {errno = err; return -1;}
  flags = fcntl(fd, F_GETFL);
  if (flags == -1) return -1;
  if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) return -1;

  return 0;
}

/*
  This function uses accept() to accept a new client who wants to connect, pairing it with its target fd.
  It also updates the global context with a new state.
  
  It returns -1 on error, otherwise the new client socket file descriptor.
*/
int accept_client() {
  if (ctx->numclients >= MAX_CLIENTS) return -1;
  
  int cfd;
  struct sockaddr_in sa;
  socklen_t slen = sizeof(sa);

  cfd = accept(ctx->serversock, (struct sockaddr*)&sa, &slen);
  if (cfd == -1) {
    ERR("accept() failed");
    return -1;
  }    

  // connect to the target and check if the operation is still in progress
  int status = -1;
  int tfd = tcp_conn_start(target->addr, target->port, &status);
  if (tfd == -1) {
    ERR("Connection to target failed");
    close(cfd);
    return -1;
  }

  int i = ctx->numclients;
  ctx->clients[i] = cfd;
  ctx->targets[i] = tfd;
  ctx->t_status[i] = status;
  ctx->numclients++;

  return cfd;
}

/* It closes both the client and target sockets, and removed their value from the context. */
void close_session(int idx) {
  close(ctx->clients[idx]);
  close(ctx->targets[idx]);

  int last = ctx->numclients - 1;
  ctx->clients[idx] = ctx->clients[last];
  ctx->targets[idx] = ctx->targets[last];
  ctx->t_status[idx] = ctx->t_status[last];
  ctx->clients[last] = 0;
  ctx->targets[last] = 0;
  ctx->t_status[last] = 0;
  ctx->numclients--;
}
