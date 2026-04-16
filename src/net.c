#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/tcp.h>

#include "net.h"

/* Init the TCP connection and returns the assigned server socket file descriptor. */
int server_init(int port) {
  struct sockaddr_in sa;
  int sfd, optval;
    
  if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(&optval));
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
int socketSetNonBlockNoDelay(int fd) {
  int flags;
  int y = 1;

  if ((flags = fcntl(fd, F_GETFL)) == -1) return -1;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;

  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &y, sizeof(y));
  return 0;
}

/*
  Connect using TCP stream to a remote target. This function supports both ipv4 and ipv6.
  If nonblock is != 0, the socket is set to a non-blocking state before calling connect().
  In this case, if connect() returns EINPROGRESS, the socket is returned to the caller, which
  will be responsible for waiting (using select) until the connection is successfully enstablished.
  
  Returns -1 on error, socket file descriptor used for connection to the remote server on success.
*/
int TCPConnect(const char *addr, int port, int nonblock) {
  struct addrinfo hints, *servinfo, *p;
  char portstr[6];
  int fd, retval = -1;

  snprintf(portstr, sizeof(portstr), "%d", port);
  memset(&hints, 0, sizeof(hints));
  
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  /* ipv4 and ipv6 support, using a list of addrinfo */
  if (getaddrinfo(addr, portstr, &hints, &servinfo) != 0) return -1;

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      continue;

    if (nonblock && socketSetNonBlockNoDelay(fd) == -1) {
      close(fd);
      break;    
    }

    if (connect(fd, p->ai_addr, p->ai_addrlen) == -1) {      
      if (errno == EINPROGRESS && nonblock) return fd;
      close(fd);
      break;
    }
    retval = fd;
    break;
  }

  return retval;
}

/*
  This function uses accept() to accept a new client who wants to connect.
  It also updates the global context with a new state.
  
  It returns -1 on error, otherwise the new client socket file descriptor.
*/
int accept_client() {
  if (ctx->numclients > MAX_CLIENTS) return -1;
  
  int cfd;
  struct sockaddr_in sa;
  socklen_t slen = sizeof(sa);

  cfd = accept(ctx->serversock, (struct sockaddr*)&sa, &slen);
  if (cfd == -1) {
    perror("accept error");
    exit(1);
  }    

  int tfd = TCPConnect(target->addr, target->port, 0);
  if (tfd == -1) {
    perror("Connection to target failed");
    close(tfd);
    return -1;
  }

  int i = ctx->numclients;
  ctx->clients[i] = cfd;
  ctx->targets[i] = tfd;
  ctx->numclients++;

  if (cfd > ctx->maxclient) ctx->maxclient = cfd;
  if (tfd > ctx->maxclient) ctx->maxclient = tfd;

  printf("Client %d <=> Target %d\n", cfd, tfd);
  
  return cfd;
}

/*
  Setup the fd_set used by select syscall with values from global context struct.
  It returns the max socket file descriptor, needed by select().
*/
int build_fd_set(fd_set *readfds) {
  FD_ZERO(readfds);
  FD_SET(ctx->serversock, readfds);
  
  /* This allows us to interact with the program */
  FD_SET(STDIN_FILENO, readfds); 
  
  for (int i = 0; i < ctx->numclients; i++) {
    if (ctx->clients[i] > 0) FD_SET(ctx->clients[i], readfds);
    if (ctx->targets[i] > 0) FD_SET(ctx->targets[i], readfds);
  }

  int maxfd = ctx->serversock;
  for (int i = 0; i < ctx->numclients; i++) {
    if (ctx->clients[i] > maxfd) maxfd = ctx->clients[i];
    if (ctx->targets[i] > maxfd) maxfd = ctx->targets[i];
  }
    
  return maxfd;
}

/* It closes both the client and target sockets, and removed their value from the context. */
void close_session(int idx) {
  printf("Closing session cfd=%d <-> tfd=%d\n", ctx->clients[idx], ctx->targets[idx]);
  close(ctx->clients[idx]);
  close(ctx->targets[idx]);

  int last = ctx->numclients - 1;
  ctx->clients[idx] = ctx->clients[last];
  ctx->targets[idx] = ctx->targets[last];
  ctx->clients[last] = 0;
  ctx->targets[last] = 0;
  ctx->numclients--;
}
