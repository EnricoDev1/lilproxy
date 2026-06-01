#include <stdint.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <lilproxy/state.h>
#include <lilproxy/net.h>
#include <lilproxy/commands.h>
#include <lilproxy/epoll.h>

typedef struct _FD_STATE {
  fd_kind kind;
  int peer_fd;
  target_status target_status;
} fd_state;

#define FDTAB_SIZE (MAX_CLIENTS * 2 + 64)

static fd_state fdtab[FDTAB_SIZE];

static void fd_state_init(int fd) {
  if (fd < 0 || fd >= FDTAB_SIZE) return;
  fdtab[fd].kind = FD_UNUSED;
  fdtab[fd].peer_fd = -1;
}

static fd_state *fdtab_get(int fd) {
  if (fd < 0 || fd >= FDTAB_SIZE) return NULL;
  return &fdtab[fd];
}

static int fd_state_set(int fd, fd_kind kind, int peer_fd, target_status status) {
  if (fd < 0 || fd >= FDTAB_SIZE) return -1;
  fdtab[fd].kind = kind;
  fdtab[fd].peer_fd = peer_fd;
  fdtab[fd].target_status = status;
  return 0;
}

static int ep_add_fd(int epfd, int fd, uint32_t events) {
  struct epoll_event ev = {0};
  ev.events = events;
  ev.data.fd = fd;
  return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static int ep_mod_fd(int epfd, int fd, uint32_t events) {
  struct epoll_event ev = {0};
  ev.events = events;
  ev.data.fd = fd;
  return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

static void close_fd(int epfd, int fd) {
  epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
  close(fd);
  fd_state_init(fd);
}

/* This function closes both client/target fds when connection terminates */
static void close_fd_pair(int epfd, int fd) {
  fd_state *s = fdtab_get(fd);
  int peer_fd = -1;

  if (s != NULL) peer_fd = s->peer_fd;
  close_fd(epfd, fd);

  if (peer_fd >= 0) {
    fd_state *peer = fdtab_get(peer_fd);
    if (peer->peer_fd >= 0)
      close(peer_fd);
  }

  if (ctx->numclients > 0) ctx->numclients--;
}

/* Init the epfd, returning the associated fd on success, -1 on error. */
int epoll_init() {
  int epfd = epoll_create1(0);

  if (epfd == -1) {
    ERR("Failed to create epoll");
    return -1;
  }

  /* Setup server listen socket and stdin fds */
  if ((fd_state_set(ctx->serversock, FD_LISTENER_TCP, -1, CONNECTED) == -1) ||
      (ep_add_fd(epfd, ctx->serversock, EPOLLIN | EP_BASE_EVENTS) == -1)) {
    ERR("epoll add server socket");
    return -1;
  }
  
  if ((fd_state_set(STDIN_FILENO, FD_STDIN, -1, CONNECTED) == -1) ||
      ep_add_fd(epfd, STDIN_FILENO, EPOLLIN | EP_BASE_EVENTS) == -1) {
    ERR("epoll add stdin");
    return -1;
  }

  return epfd;
}

/* Helper function that adds the client/target pair to epfd and fdtab */
static int add_proxy_pair(int epfd, int client_fd, int target_fd, target_status status) {
  uint32_t client_evts = (status == CONNECTING) ? EP_BASE_EVENTS : (EPOLLIN | EP_BASE_EVENTS);
  uint32_t target_evts = (status == CONNECTING) ? (EPOLLOUT | EP_BASE_EVENTS) : (EPOLLIN | EP_BASE_EVENTS);

  if (fd_state_set(client_fd, FD_PROXY_CLIENT, target_fd, CONNECTED) == -1 ||
      fd_state_set(target_fd, FD_PROXY_TARGET, client_fd, status) == -1) {
    goto fail;
  }

  if (ep_add_fd(epfd, client_fd, client_evts) == -1)
    goto fail;

  if (ep_add_fd(epfd, target_fd, target_evts) == -1)
    goto fail;

  ctx->numclients++;
  return 0;

  fail:
    close(client_fd);
    close(target_fd);
    fd_state_init(client_fd);
    fd_state_init(target_fd);
    return -1;
}

static int handle_tcp_listener(int epfd) {
  int client_fd;
  int target_fd;
  target_status status;

  if (accept_proxy_pair(&client_fd, &target_fd, &status) == -1) return -1;
  return add_proxy_pair(epfd, client_fd, target_fd, status);
}

/* This function completes a pending non-blocking target connect before allowing relay. */
static int handle_pending_target(int epfd, int fd, fd_state *st, uint32_t ev) {
  if (st->kind != FD_PROXY_TARGET || st->target_status != CONNECTING) return 0;
  if (!(ev & (EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP))) return 1;

  if (check_target_connect(fd) == -1) {
    close_fd_pair(epfd, fd);
    return 1;
  }

  /* Make both client and target ready for communication. */
  st->target_status = CONNECTED;
  if (ep_mod_fd(epfd, fd, EPOLLIN | EP_BASE_EVENTS) == -1 ||
      ep_mod_fd(epfd, st->peer_fd, EPOLLIN | EP_BASE_EVENTS) == -1) {
    close_fd_pair(epfd, fd);
  }

  return 1;
}

static void handle_proxy_pair(int epfd, int fd, fd_state *st, uint32_t ev) {
  if (handle_pending_target(epfd, fd, st, ev)) return;

  if (ev & (EPOLLHUP | EPOLLERR)) {
    close_fd_pair(epfd, fd);
    return; 
  }

 /* We only want to close the connection on EPOLLRDHUP when there's no EPOLLIN.
    That's because when epoll reports EPOLLIN | EPOLLRDHUP it means that there's still unread data in the socket buffer.   */
  if(!(ev & EPOLLIN)) {
    if (ev & EPOLLRDHUP) close_fd_pair(epfd, fd);
    return;
  }

  fd_state *peer = fdtab_get(st->peer_fd);
  if (peer == NULL || peer->peer_fd != fd || peer->kind == FD_UNUSED) {
    close_fd_pair(epfd, fd);
    return;
  }
  
  endpoint_role sender = (st->kind == FD_PROXY_CLIENT) ? EP_CLIENT : EP_TARGET;
  if (relay_once(fd, st->peer_fd, sender) == -1) {
    close_fd_pair(epfd, fd);
    return;
  }
}

/* Iterates all ready events, performing different actions based on event type. */
void handle_events(int epfd, struct epoll_event *evts, int ne) {
  for (int i = 0; i < ne; i++) {
    int fd = evts[i].data.fd;
    uint32_t ev = evts[i].events;
    fd_state *st = fdtab_get(fd);

    if (st == NULL || st->kind == FD_UNUSED) continue;

    switch(st->kind) {
      case FD_LISTENER_TCP:
        if (handle_tcp_listener(epfd) == -1)
          WARN("tcp listener event handling failed");
        continue;
      case FD_STDIN:
        if (read_command() == -1)
          WARN("stdin command handling failed");
        continue;
      case FD_PROXY_CLIENT:
      case FD_PROXY_TARGET:
        handle_proxy_pair(epfd, fd, st, ev);
        continue;
      case FD_LISTENER_CMD:
      case FD_CMD_CLIENT:
      case FD_UNUSED:
        continue;
    }
  }
}
