#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/tcp.h>

#include "lib.h"

proxyContext *ctx;
proxyTarget *target;
blacklist *bl;

/* Allocate the proxy context struct with error handling */ 
void *ctx_malloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    perror("Memory error while malloc");
    exit(1);
  }
  return ptr;
}

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

/* Initialize the proxy context to default values. */
void connection_init(int port) {
  ctx = ctx_malloc(sizeof(*ctx));
  memset(ctx, 0, sizeof(*ctx));
  
  /* we don't have any client connected yet */
  ctx->numclients = 0;
  ctx->maxclient = -1; 
  ctx->serversock = server_init(port); 
  
  if (ctx->serversock == -1) {
    perror("Creating listening socket");
    exit(1);
  }
}

/* Initialize proxy state with values received from command line. */
void proxy_init(const char *addr, int port) {
  target = malloc(sizeof(*target));
  memset(target, 0, sizeof(*target));
  snprintf(target->addr, ADDR_LEN, "%s", addr);
  target->port = port;
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


/*
  This function loads rules from config.txt file, filling up the global blacklist.
  It also checks for allocation error.
  
  TODO: add support for binary strings.
*/
void load_rules() {
  bl = malloc(sizeof(*bl));
  if (bl == NULL) {perror("malloc bl"); exit(1);}
  
  bl->nrules = 0;
  bl->capacity = 20;
  bl->rules = malloc(sizeof(rule *));
  if (bl->rules == NULL) {perror("malloc bl->rules"); exit(1);}

  FILE *fp = fopen("config.txt", "r");
  if (fp == NULL) {perror("Error while opening config.txt file"); exit(1);}

  /* Read rules from file and fill the blacklist */
  char line[MAX_PATTERN_LEN];
  while (fgets(line, MAX_PATTERN_LEN, fp)) {
    int len = atoi(strtok(line, ":"));
    char *pattern = strtok(NULL, "\n");
    if (pattern == NULL) continue;

    /* Reallocate space if needed */
    if (bl->nrules == bl->capacity) {
      bl->capacity *= 2;
      rule **tmp = realloc(bl->rules, bl->capacity*sizeof(rule *));
      if (tmp == NULL) {perror("realloc bl->rules"); exit(1);}
      bl->rules = tmp; 
    }

    /* Create new rule and append it to blacklist. */
    rule *r = malloc(sizeof(*r));
    if (r == NULL) {perror("malloc rule"); exit(1);}
    
    r->len = len;
    r->pattern = malloc(len+1 * sizeof(char));
    if (r->pattern == NULL) {perror("malloc r->pattern"); exit(1);}
    snprintf(r->pattern, MAX_PATTERN_LEN, "%s", pattern);
    bl->rules[bl->nrules++] = r;
  }
  fclose(fp);
}

int is_forbidden(const char *buf) {
  for (int i = 0; i < bl->nrules; i++) {
    if (strstr(buf, bl->rules[i]->pattern)) {
      return -1;
    }
  }
  return 0;
}

/*
  Send data received from src to dst. It handles partial writes by looping untile all bytes have been sent. 
  Returns -1 on disconnections/errors, 0 otherwise.

  NOTE: int sender it isn't used at the moment, but it could be useful in future.
*/
int relay(int src, int dst, int sender) {
  char buf[MAX_READ_SIZE];
  int n = read(src, buf, sizeof(buf)-1);
  
  if (n <= 0) return -1;
    
  buf[n] = '\0';
  
  if (is_forbidden(buf)) return 0;  
  
  int wrtn = 0;
  while (wrtn < n) {
    int w = write(dst, buf + wrtn, n - wrtn);
    if (w <= 0) return -1;
    wrtn += w;
  }
  
  return 0;
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

int main(int argc, char *argv[]) {
  char addr[ADDR_LEN];
  int r_port = 0, l_port = 0;
  
  parse_args(argc, argv, addr, &r_port, &l_port);

  if (l_port == 0) {
    fprintf(stderr, "Usage: %s -l <local-port> -a <remote-addr> -p <remote-port>", argv[0]);
    exit(1);
  }
  
  connection_init(l_port);
  proxy_init(addr, r_port);
  load_rules();
    
  struct timeval tv;
  fd_set readfds;
  
  while (1) {
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int maxfd = build_fd_set(&readfds);
    int retval = select(maxfd+1, &readfds, NULL, NULL, &tv);
    
    if (retval == -1) {
      perror("select() error");
      exit(1);
    }

    /* One or more file descriptors are ready. */
    if (retval) {
      /* A new client wants to connect. */
      if (FD_ISSET(ctx->serversock, &readfds)) {
        accept_client();
      }

      /* For each active session, relay data in both directions. */
      for (int i = 0; i < ctx->numclients; i++) {
        if (ctx->clients[i] && FD_ISSET(ctx->clients[i], &readfds)) {
          if (relay(ctx->clients[i], ctx->targets[i], CLIENT_SENDER) == -1) {
            close_session(i--);
            continue;
          }
        }
        if (i >= 0 && ctx->targets[i] > 0 && FD_ISSET(ctx->targets[i], &readfds)) {
          if (relay(ctx->targets[i], ctx->clients[i], TARGET_SENDER) == -1) {
            close_session(i--);
          }
        }
      }      
    } else {
      /* Timeout reached... */
    }
  }  
  return 0;
}

