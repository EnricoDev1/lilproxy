#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>

#include <proxy/rules.h>
#include <proxy/types.h>
#include <proxy/commands.h>
#include <proxy/state.h>
#include <proxy/term.h>

struct cmdBuffer cb;

static struct cmdBuffer *history[HISTORY_LEN];
static size_t h_len = 0;
static size_t h_pos = 0;

static struct commandEntry commands[MAX_COMMANDS];
static int commands_len = 0;

int grows, gcols;

/* ========================== low level terminal handling ========================== */
static void disable_raw_mode_at_exit(void);
static void cb_clear();
static void draw_prompt_buf();

int set_raw_mode(int fd, int enable) {
  static struct termios g_orig_term;
  static int raw_mode_on = 0;
  static int atexit_reg = 0;  
  struct termios raw;
  
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
  draw_prompt_buf();
  return 0;
}

static void disable_raw_mode_at_exit(void) {
  set_raw_mode(STDIN_FILENO, 0);
}

static void clear_screen() {
  write(STDOUT_FILENO, CLEAR, 7);
}

static void clear_cur_line() {
  write(STDOUT_FILENO, ERASE_LINE, 4);
}

static void cursor_at_line_start() {
  write(STDOUT_FILENO, "\r", 1);
}

static void draw_prompt() {
  clear_cur_line();
  cursor_at_line_start();
  write(STDOUT_FILENO, PROMPT_CHAR" ", 2);
}

static void draw_prompt_buf() {
  draw_prompt();
  if (cb.len > 0) {
    write(STDOUT_FILENO, cb.cmd, cb.len);
  }
}

static int get_term_rowcol() {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return -1;
  gcols = ws.ws_col;
  grows = ws.ws_row;
  return 0;
}

/* ========================== history handling ========================== */
/* This function adds the current buffer content to the history when a command is sent. */
static int history_add_cmd() {
  if (h_len > 0 && (strcmp(history[h_len-1]->cmd, cb.cmd) == 0)) return -1;
  
  if (history[h_len] != NULL) free(history[h_len]);
  history[h_len] = malloc(sizeof(struct cmdBuffer));
  if (history[h_len] == NULL) {perror("malloc history[h_len]"); return -1;}
    
  strncpy(history[h_len]->cmd, cb.cmd, cb.len);
  history[h_len]->cmd[cb.len] = '\0';
  history[h_len]->len = cb.len;
  
  h_len = (h_len+1)%HISTORY_LEN;
  h_pos = h_len;

  return 0;
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
  for (size_t i = 0; i < CMD_MAX; i++) cb.cmd[i] = '\0';
}

/* This function update the global command buffer fields with data from the current history selected entry. */
static void update_cb_history(struct cmdBuffer *buf) {
  memset(cb.cmd, 0, sizeof(cb.cmd));
  strncpy(cb.cmd, buf->cmd, buf->len);
  cb.len = buf->len;
}

/* Print a binary string parsing non-printable raw bytes as byte string format. It also truncates the output if it's too long */
static void print_bin(const char *input, size_t len) {
  if (get_term_rowcol() == -1) return;
  size_t max_len = (gcols > 20) ? (gcols - 40) : gcols;
  size_t shown = 0;

  for (size_t i = 0; i < len; i++) {
    unsigned char b = input[i];
    if (shown >= max_len) {
      printf(COLOR_WARN "...[truncated %zu bytes]" RESET, len-i);
      break;
    }

    switch (b) {
      case '\n':
        printf("\\n");
        shown += 2;
        break;
      case '\r':
        printf("\\r");
        shown += 2;
        break;
      case '\t':
        printf("\\t");
        shown += 2;
        break;
      case '\\':
        printf("\\\\");
        shown += 2;
        break;
      default:
        if (isprint(b)) {
          fputc(b, stdout);
          shown += 1;
        } else {
          printf("\\x%02x", b);
          shown += 4;
        }
        break;
    }
  }
}

/* ========================== Commands callbacks ========================== */
static void print_rules() {
  if (bl->nrules == 0) {
    printf(COLOR_INFO "No rules loaded." RESET "\r\n");
    fflush(stdout);
    return;
  }

  for (int i = 0; i < bl->nrules; i++) {
    rule *r = bl->rules[i];
    printf(DIM "#%d " RESET, r->id);
    if (r->action == ACTION_REPLY) {
      printf("reply:");
      print_bin(r->pattern, r->p_len);
      printf(":");
      if (r->reply_src == REPLY_SRC_FILE) {
        printf("@file=%s", r->r_file_path);
      } else print_bin(r->response, r->r_len);
    }
    else if (r->action == ACTION_BLOCK) {
      printf("block:");
      print_bin(r->pattern, r->p_len);
    } else if (r->action == ACTION_DROP) {
      printf("drop:");
      print_bin(r->pattern, r->p_len);
    }
    printf("\r\n");
  }   
  fflush(stdout);
}

static void print_help() {
  printf(COLOR_HEADER "Available commands" RESET "\r\n");
  printf(COLOR_INFO "  addrule <action>:<pattern>:[<response>]" RESET " - add a new rule\r\n");
  printf(COLOR_INFO "  del <rule_id>" RESET " - delete a rule\r\n");
  printf(COLOR_INFO "  lsrules" RESET " - list current blacklist\r\n");
  printf(COLOR_INFO "  clear" RESET " - clear screen\r\n");
  printf(COLOR_INFO "  help" RESET " - show this help\r\n");
}

/* This function add a new rule to the global blacklist */
static void add_rule() {
  char err[ERROR_LEN];
  rule *r = NULL;
  char *args = cb.cmd + 8;

  if (rule_parse_line(args, &r, err) == -1) {
    ERR("error: %s", err);
    return;
  }

  if (add_rule_to_bl(r) == -1) return;
  printf(COLOR_SUCCESS "[+] Rule successfully added" RESET "\r\n");
  return;
}

/* Delete a rule from blacklist based on provided ID */
static void del_rule() {
  if (cb.len < 5) {
    ERR("error: missing id");
    return;
  }
  
  char *arg = cb.cmd + 4;
  char *end;
  int id = strtol(arg, &end, 10);

  if (end == arg || *end != '\0') {
    ERR("error: invalid id");
    return;
  }

  if (errno == ERANGE) {
    ERR("error: id out of max range");
    return;
  }
  
  if(del_rule_by_id(id) == -1) {
    WARN("no rule found for id=%d", id);
    return; 
  }  

  printf(COLOR_SUCCESS "[+] Rule successfully deleted" RESET "\r\n");
}

static void add_command(char *command_name, void (*callback)(void)) {
  if (commands_len >= MAX_COMMANDS) return;
  commands[commands_len].name = command_name;  
  commands[commands_len].callback = callback;
  commands_len++;
}

/* This function simply calls the callback of a command based on the command name received as argument */
static void exec_command(const char *command) {
  for(int i = 0; i < commands_len; i++) {
    if (strcmp(command, commands[i].name) == 0) {
      commands[i].callback();
      return;
    }
  }
  ERR("unknown command: %s", command);
}

/* This function call the exec_command() with the right command name */
static void parse_command() {
  char *cmd = strtok(cb.cmd, " ");
  if (cmd == NULL) return;
  exec_command(cmd);
}

void commands_init() {
  add_command("addrule", add_rule);
  add_command("del", del_rule);
  add_command("lsrules", print_rules);
  add_command("clear", clear_screen);
  add_command("help", print_help);
}

/* ========================== Input handling and command parsing ========================== */
/* This function parse the global command buffer calling functions based on its content. */

/* This function reads a single char from stdin and returns it. It aso handle basic escape sequences parsing. */
static int read_key() {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {ERR("read"); return -1;}
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

  if (c == '\b' || c == 127) return DEL_KEY;
    
  return c;
}

/*
  This function reads from stdin, handling one command line in terminal raw mode.
*/
int read_command() {
  int ch = read_key();

  if (ch == -1) return -1;

  /* handle command submission */ 
  if (ch == '\r' || ch == '\n') {
    cb.cmd[cb.len] = '\0';
    write(STDOUT_FILENO, "\r\n", 2);

    if (history_add_cmd() == -1) return -1;
    parse_command();
    cb_clear();
    draw_prompt_buf();
  }
  
  if (ch == DEL_KEY && cb.len > 0) {
    cb_del();
    draw_prompt_buf();
  }

  /* append input char to global command buffer and write it on screen. */
  if (isprint((unsigned char)ch) && cb.len < CMD_MAX - 1) {
    cb_append(ch);
    write(STDOUT_FILENO, &ch, 1);
  }

  /* ctrl keys handling */
  if (ch == CTRL_KEY('l')) {
    clear_screen();
    draw_prompt();
  }

  /* history keys handling */
  if (ARROW_KEY(ch)) {
    struct cmdBuffer *buf = NULL;
    switch (ch) {
      case ARROW_UP:
        buf = history_get_prev();
        break;
      case ARROW_DOWN:
        buf = history_get_next();
        if (buf == NULL) draw_prompt();
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
