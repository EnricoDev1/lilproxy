#include <ctype.h>
#include <rpc/netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "rules.h"
#include "types.h"
#include "commands.h"

#define CTRL_KEY(k) ((k) & 0x1f)
#define ARROW_KEY(k) ((k) >= ARROW_LEFT && (k) <= ARROW_DOWN)

enum specialKeys {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

struct cmdBuffer cb;
static struct cmdBuffer *history[HISTORY_LEN];
static size_t h_len = 0;
static size_t h_pos = 0;

/* ========================== "TUI" setup ========================== */
void disable_raw_mode_at_exit(void);
static void cb_clear();
static void redraw_prompt();

int set_raw_mode(int fd, int enable) {
  static struct termios g_orig_term;
  static int raw_mode_on = 0;
  static int atexit_reg = 0;
  
  struct termios raw;
  struct cmdBuffer buf;
  if (enable == 0) {
    if (raw_mode_on && tcsetattr(fd, TCSAFLUSH, &g_orig_term) == 0) {
      raw_mode_on = 0;
    }
    return 0;
  }

  if (!isatty(fd)) return -1;
  if (!atexit_reg) {
    atexit(disable_raw_mode_at_exit);
    atexit_reg = 1;
  }

  if (tcgetattr(fd, &g_orig_term) == -1) return -1;

  raw = g_orig_term;
  /* input modes: no break (SIGINT), no CR to LF, no parity check, no strip char to 7 bit, no start/stop output control */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /* control modes: set 8 bit chars */
  raw.c_cflag |= CS8;
  /* local modes: no echo - canonical off (no kernel buffering) - no extended functions (keep ^Z and ^C working) */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
  /* we want read to return EVERY single byte, without any buffering */
  raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSAFLUSH, &raw) == -1) return -1;

  raw_mode_on = 1;
  cb_clear();
  redraw_prompt();
  return 0;
}

void disable_raw_mode_at_exit(void) {
  set_raw_mode(STDIN_FILENO, 0);
}

static void clear_screen() {
  write(STDOUT_FILENO, "\x1b[H\x1b[2J\x1b[3J", 11);
}

static void clear_cur_line() {
  write(STDOUT_FILENO, "\x1b[2K", 4);
}

static void cursor_at_line_start() {
  write(STDOUT_FILENO, "\r", 1);
}

static void draw_prompt() {
  cursor_at_line_start();
  clear_cur_line();
  write(STDOUT_FILENO, "❯ ", 4);
}

static void redraw_prompt() {
  draw_prompt();
  if (cb.len > 0) {
    write(STDOUT_FILENO, cb.cmd, cb.len);
  }
}

/* ========================== HISTORY ========================== */
/* This function adds the current buffer content to the history when a command is sent. */
static void history_add_cmd() {
  if (h_len > 0 && (strcmp(history[h_len-1]->cmd, cb.cmd) == 0)) return;

  if (history[h_len] != NULL) free(history[h_len]);
  history[h_len] = malloc(sizeof(struct cmdBuffer));
  if (history[h_len] == NULL) {perror("malloc history[h_len]"); exit(1);}
   
  strncpy(history[h_len]->cmd, cb.cmd, cb.len);
  history[h_len]->cmd[cb.len] = '\0';
  history[h_len]->len = cb.len;
  
  h_len = (h_len+1)%HISTORY_LEN;
  h_pos = h_len;
} 

/* This function returns the next cmdBuffer in the global history list. */
static struct cmdBuffer *history_get_next() {
  if (h_pos == h_len) return NULL;
  h_pos = (h_pos+1)%HISTORY_LEN;
  return history[h_pos];
}

/* This function returns the previous cmdBuffer in the global history list. */
static struct cmdBuffer *history_get_prev() {
  size_t prev_idx = (h_pos + HISTORY_LEN - 1) % HISTORY_LEN;
  if (history[prev_idx] == NULL || prev_idx == h_len) return NULL;
  h_pos = prev_idx;
  return history[h_pos];
}

/* ========================== Command Buffer ========================== */
/* Append a char to the cmd buffer if there's enough space. */
static void cb_append(char c) {
  if (cb.len >= CMD_MAX - 1) return;
  cb.cmd[cb.len++] = c;
  cb.cmd[cb.len] = '\0';
}

/* Delete a char from the command buffer. */
static void cb_del() {
  if (cb.len == 0) return;
  cb.len--;
  cb.cmd[cb.len] = '\0';
}

/* Initialize the command buffer and its length to 0. */
static void cb_clear() {
  cb.len = 0;
  cb.cmd[0] = '\0';
}

/* This function update the global command buffer fields with data from the current history selected entry. */
static void update_cb_history(struct cmdBuffer *buf) {
  memset(cb.cmd, 0, sizeof(cb.cmd));
  strncpy(cb.cmd, buf->cmd, buf->len);
  cb.len = buf->len;
}

/* ========================== Commands callbacks ========================== */
/* This function print rules based on their action */
static void print_rules() {
  for (int i = 0; i < bl->nrules; i++) {
    rule *r = bl->rules[i];
    if (r->action == ACTION_REPLY) printf("reply:%s:%s\n", r->pattern, r->response);
    else if (r->action == ACTION_BLOCK) printf("block:%s\n", r->pattern);
    else if (r->action == ACTION_DROP) printf("drop:%s\n", r->pattern);
  }
}

/* ========================== Input handling and command parsing ========================== */
/* This function parse the global command buffer calling functions based on its content. */
static void parse_command() {
  if (strncmp(cb.cmd, "/addrule ", 9) == 0) {
    char err[ERROR_LEN];
    rule *r = NULL;
    char *args = cb.cmd + 9;

    if (rule_parse_line(args, &r, err) == -1) {
      ERR("error: %s", err);
      return;
    }

    add_rule_to_bl(r);
    printf("[*] Rule successfully added\n");
    return;
  }

  if (strcmp(cb.cmd, "/lsrules") == 0) {
    print_rules();
    return;
  }

  if (strcmp(cb.cmd, "/clear") == 0) {
    clear_screen();
    return;
  }

  if (strcmp(cb.cmd, "/exit") == 0) exit(0);
}

/* This function reads a single char from stdin and returns it. It aso handle basic escape sequences parsing. */
int read_key() {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {ERR("read"); exit(1);}
  }
  
  /* Parse escape sequence. */
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
      }
    }
    return '\x1b';
  }
  
  return c;
}

/*
  This function reads from stdin, handling one command line in terminal raw mode.
  TODO: implement line whitespace strip
*/
int read_command() {
  int ch = read_key();
  
  if (ch == '\r' || ch == '\n') {
    cb.cmd[cb.len] = '\0';
    write(STDOUT_FILENO, "\r\n", 2);
    parse_command();
    history_add_cmd();
    cb_clear();
    redraw_prompt();
  }

  if (ch == 127 || ch == '\b') {
    if (cb.len > 0) {
      cb_del();
      redraw_prompt();
    }
  }

  if (isprint((unsigned char)ch) && cb.len < CMD_MAX - 1) {
    cb_append(ch);
    write(STDOUT_FILENO, &ch, 1);
  }

  if (ch == CTRL_KEY('l')) {
    clear_screen();
    draw_prompt();
  }

  if (ARROW_KEY(ch)) {
    struct cmdBuffer *buf = NULL;
    switch (ch) {
      case ARROW_UP:
        buf = history_get_prev();
        break;
      case ARROW_DOWN:
        buf = history_get_next();
        break;
    }
    if (buf != NULL) {
      draw_prompt();
      write(STDOUT_FILENO, buf->cmd, buf->len);
      update_cb_history(buf);
    }
  }
  
  return 0;
}
