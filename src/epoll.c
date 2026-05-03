#include <sys/epoll.h>
#include <unistd.h>

#include <proxy/state.h>
#include <proxy/net.h>
#include <proxy/commands.h>
#include <proxy/epoll.h>

/* add a new socket file descriptor to epfd */
static int ep_add_fd(int epfd, int fd, uint32_t events) {
   struct epoll_event ev = {0};
   ev.events = events;
   ev.data.fd = fd;
   return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

/* modify an exsting epfd socket file descriptor */
static int ep_mod_fd(int epfd, int fd, uint32_t events) {
   struct epoll_event ev = {0};
   ev.events = events;
   ev.data.fd = fd;
   return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

static int get_fd_info(int fd, endpoint_role *role) {
  for (int i = 0; i < ctx->numclients; i++) {
    if (ctx->clients[i] == fd) {*role = EP_CLIENT; return i;}
    if (ctx->targets[i] == fd) {*role = EP_TARGET; return i;}
  }
  return -1;
}

/* Init the epfd, returning the associated fd on success, -1 on error. */
int epoll_init() {
  int epfd = epoll_create1(0);

  if (epfd == -1) {
    ERR("Failed to create epoll");
    return -1;
  }

  if (ep_add_fd(epfd, ctx->serversock, EPOLLIN | EP_BASE_EVENTS) == -1) {
    ERR("epoll add server socket");
    return -1;
  }

  if (ep_add_fd(epfd, STDIN_FILENO, EPOLLIN | EP_BASE_EVENTS) == -1) {
    ERR("epoll add stdin");
    return -1;
  }

  return epfd;
}

/* This function is responsible for handling the new session, adding the client and target file descriptors to epfd. */
static void handle_session(int epfd, int cfd) {
  int idx = ctx->numclients - 1;
  int tfd = ctx->targets[idx];
  int t_status = ctx->t_status[idx]; // connected or connecting

  /*
    If the target is still connecting, we don't to enable EPOLLIN on the client, and on the target we wait with EPOLLOUT so that we can know when the connect finish.
    Otherwise, we enable EPOLLIN on both on client and target.
  */
  uint32_t client_evts = (t_status == CONNECTING) ? EP_BASE_EVENTS : (EPOLLIN | EP_BASE_EVENTS);
  uint32_t target_evts = (t_status == CONNECTING) ? (EPOLLOUT | EP_BASE_EVENTS) : (EPOLLIN | EP_BASE_EVENTS);
  
  // try to add file descriptors to epfd, if they fail they are removed
  if (ep_add_fd(epfd, cfd, client_evts) == -1 || ep_add_fd(epfd, tfd, target_evts) == -1) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    epoll_ctl(epfd, EPOLL_CTL_DEL, tfd, NULL);
    close_session(idx);
  }  
}

/* Remove the client and target file descriptors from epfd and terminate the connection between them. */
static void close_and_clean(int epfd, int idx) {
  epoll_ctl(epfd, EPOLL_CTL_DEL, ctx->clients[idx], NULL);
  epoll_ctl(epfd, EPOLL_CTL_DEL, ctx->targets[idx], NULL);
  close_session(idx);
}

/* Setup file descriptors, indexes and infos about client and target */
static int setup_fds(int fd, relay_ctx *rc) {
  rc->idx = get_fd_info(fd, &rc->role);
  if (rc->idx < 0) return -1;
  
  rc->srcfd = (rc->role == EP_CLIENT) ? ctx->clients[rc->idx] : ctx->targets[rc->idx];
  rc->dstfd = (rc->role == EP_CLIENT) ? ctx->targets[rc->idx] : ctx->clients[rc->idx];
  
  return 0;
}

/* This function is called once the target finished the connection process, and it updates the states so that it's ready to receive/send data. */
static void update_target_status(int epfd, int idx) {
  ctx->t_status[idx] = CONNECTED;
  if (ep_mod_fd(epfd, ctx->clients[idx], EPOLLIN | EP_BASE_EVENTS) == -1 ||
      ep_mod_fd(epfd, ctx->targets[idx], EPOLLIN | EP_BASE_EVENTS) == -1) {
    close_and_clean(epfd, idx);
  }
}

/* Handle events not relating a specific endpoint data: just new connections end and commands from cli. */
static int handle_control_event(int epfd, int fd) {
  if (fd == ctx->serversock) {
    int cfd = accept_client();
    if (cfd != -1) handle_session(epfd, cfd);
    return 1;
  }

  if (fd == STDIN_FILENO) {
    read_command();
    return 1;
  }

  return 0;
}

/* Handle the target connection process */
static int handle_target_conn_ev(int epfd, int fd, uint32_t ev, int idx) {
  if (ctx->t_status[idx] != CONNECTING) return 0;
  if (!(ev & EPOLLOUT)) return 0;

  if (target_conn_finish(fd) == -1) {
    close_and_clean(epfd, idx);
    return 1;
  }

  update_target_status(epfd, idx);
  return 1;
}

/* Handle a ready endpoint relating event. If everything is ok, relay is called between client and target, otherwise just return to caller. */
static void handle_endpoint_ev(int epfd, int fd, uint32_t ev) {
  relay_ctx rc;

  if (setup_fds(fd, &rc) == -1) return;

  if (ev & (EPOLLHUP | EPOLLERR)) {
    close_and_clean(epfd, rc.idx);
    return;
  }

  /* check the target connection status */
  if(rc.role == EP_TARGET && handle_target_conn_ev(epfd, fd, ev, rc.idx)) return;
  if (ctx->t_status[rc.idx] == CONNECTING) return;

  /* proceed only if there's something to read */  
  if (!(ev & EPOLLIN)) return;
  
  /* everything is okay, role will be used as sender */
  if (relay(rc.srcfd, rc.dstfd, rc.role) == -1) close_and_clean(epfd, rc.idx);
}

/* Iterates all ready events, performing differents actions based on event type. */
void handle_events(int epfd, struct epoll_event *evts, int ne) {
  for (int i = 0; i < ne; i++) {
    int fd = evts[i].data.fd;
    uint32_t ev = evts[i].events;

    if (handle_control_event(epfd, fd)) continue;
    handle_endpoint_ev(epfd, fd, ev);
  }
}
