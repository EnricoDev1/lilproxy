#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linenoise.h>

#include <lilproxy/commands.h>
#include <lilproxy/rules.h>
#include <lilproxy/state.h>
#include <lilproxy/types.h>

static struct commandEntry commands[MAX_COMMANDS];
static int commands_len = 0;

static struct linenoiseState ln_state;
static char ln_buf[CMD_MAX];
static int ln_started = 0;
static int ln_initialized = 0;
static char history_path[512];
static int history_path_ready = 0;

static int grows, gcols;
static int start_linenoise_editor(void);

static const char *get_history_path(void) {
  const char *home;

  if (history_path_ready) return history_path;

  home = getenv("HOME");
  if (home != NULL && *home != '\0') {
    snprintf(history_path, sizeof(history_path), "%s/.lilproxy.history", home);
  } else {
    snprintf(history_path, sizeof(history_path), ".lilproxy.history");
  }
  history_path_ready = 1;
  return history_path;
}

static void save_history_file(void) {
  const char *path = get_history_path();
  if (linenoiseHistorySave(path) == -1) {
    WARN("cannot save history file: %s", path);
  }
}

static void exit_app(void) {
  if (ln_started) {
    linenoiseEditStop(&ln_state);
    ln_started = 0;
  }
  save_history_file();
  exit(0);
}

static int get_term_rowcol(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return -1;
  gcols = ws.ws_col;
  grows = ws.ws_row;
  return 0;
}

static const char *extract_rule_expr(const char *buf, int *has_addrule_prefix) {
  if (strncmp(buf, "addrule ", 8) == 0) {
    *has_addrule_prefix = 1;
    return buf + 8;
  }
  *has_addrule_prefix = 0;
  return buf;
}

static void completion_cb(const char *buf, linenoiseCompletions *lc) {
  const char *space;
  size_t prefix_len;
  int has_addrule_prefix;
  const char *rule_expr;

  if (buf == NULL) return;
  space = strchr(buf, ' ');
  rule_expr = extract_rule_expr(buf, &has_addrule_prefix);

  if (space == NULL) {
    prefix_len = strlen(buf);
    for (int i = 0; i < commands_len; i++) {
      const char *name = commands[i].name;
      if (strncmp(name, buf, prefix_len) == 0) {
        linenoiseAddCompletion(lc, name);
      }
    }
    return;
  }

  if (strncmp(buf, "addrule ", 8) == 0) {
    static const char *addrule_samples[] = {
      "addrule <action>:<pattern>[:<response>]",
      "addrule reply:<pattern>:<response>",
      "addrule block:<pattern>",
      "addrule drop:<pattern>",
      "addrule block:",
      "addrule drop:",
      "addrule reply:"
    };
    prefix_len = strlen(buf);
    for (size_t i = 0; i < sizeof(addrule_samples) / sizeof(addrule_samples[0]); i++) {
      if (strncmp(addrule_samples[i], buf, prefix_len) == 0) {
        linenoiseAddCompletion(lc, addrule_samples[i]);
      }
    }
    return;
  }

  if (strncmp(buf, "reply:", 6) == 0 || strncmp(buf, "block:", 6) == 0 || strncmp(buf, "drop:", 5) == 0) {
    static const char *rule_samples[] = {
      "reply:<pattern>:<response>",
      "block:<pattern>",
      "drop:<pattern>"
    };
    prefix_len = strlen(buf);
    for (size_t i = 0; i < sizeof(rule_samples) / sizeof(rule_samples[0]); i++) {
      if (strncmp(rule_samples[i], buf, prefix_len) == 0) {
        linenoiseAddCompletion(lc, rule_samples[i]);
      }
    }
    return;
  }

  if (strncmp(rule_expr, "reply:", 6) == 0) {
    const char *pattern = rule_expr + 6;
    if (*pattern == '\0') return;
    if (strchr(pattern, ':') == NULL) {
      char candidate[CMD_MAX + 32];
      if (has_addrule_prefix) {
        snprintf(candidate, sizeof(candidate), "addrule reply:%s:<response>", pattern);
      } else {
        snprintf(candidate, sizeof(candidate), "reply:%s:<response>", pattern);
      }
      if (strncmp(candidate, buf, strlen(buf)) == 0) {
        linenoiseAddCompletion(lc, candidate);
      }
    }
    return;
  }

  if (strncmp(buf, "del ", 4) == 0) {
    char candidate[64];
    prefix_len = strlen(buf);
    for (int i = 0; i < bl->nrules; i++) {
      snprintf(candidate, sizeof(candidate), "del %d", bl->rules[i]->id);
      if (strncmp(candidate, buf, prefix_len) == 0) {
        linenoiseAddCompletion(lc, candidate);
      }
    }
  }
}

static char *hints_cb(const char *buf, int *color, int *bold) {
  int has_addrule_prefix;
  const char *rule_expr;

  if (buf == NULL) return NULL;
  rule_expr = extract_rule_expr(buf, &has_addrule_prefix);
  (void)has_addrule_prefix;

  *color = 90;
  *bold = 0;

  if (strchr(buf, ' ') == NULL) {
    size_t prefix_len = strlen(buf);
    for (int i = 0; i < commands_len; i++) {
      const char *name = commands[i].name;
      size_t name_len = strlen(name);
      if (name_len > prefix_len && strncmp(name, buf, prefix_len) == 0) {
        return strdup(name + prefix_len);
      }
    }
    return NULL;
  }

  if (strncmp(buf, "addrule ", 8) == 0) {
    static const char *addrule_samples[] = {
      "addrule <action>:<pattern>[:<response>]",
      "addrule reply:<pattern>:<response>",
      "addrule block:<pattern>",
      "addrule drop:<pattern>",
      "addrule block:",
      "addrule drop:",
      "addrule reply:"
    };
    size_t prefix_len = strlen(buf);
    for (size_t i = 0; i < sizeof(addrule_samples) / sizeof(addrule_samples[0]); i++) {
      size_t sample_len = strlen(addrule_samples[i]);
      if (sample_len > prefix_len && strncmp(addrule_samples[i], buf, prefix_len) == 0) {
        return strdup(addrule_samples[i] + prefix_len);
      }
    }
  }

  if (strncmp(buf, "reply:", 6) == 0 || strncmp(buf, "block:", 6) == 0 || strncmp(buf, "drop:", 5) == 0) {
    static const char *rule_samples[] = {
      "reply:<pattern>:<response>",
      "block:<pattern>",
      "drop:<pattern>"
    };
    size_t prefix_len = strlen(buf);
    for (size_t i = 0; i < sizeof(rule_samples) / sizeof(rule_samples[0]); i++) {
      size_t sample_len = strlen(rule_samples[i]);
      if (sample_len > prefix_len && strncmp(rule_samples[i], buf, prefix_len) == 0) {
        return strdup(rule_samples[i] + prefix_len);
      }
    }
  }

  if (strncmp(rule_expr, "reply:", 6) == 0) {
    const char *pattern = rule_expr + 6;
    if (*pattern == '\0') return strdup("<pattern>:<response>");
    if (strchr(pattern, ':') == NULL) return strdup(":<response>");
    return NULL;
  }

  if (strncmp(rule_expr, "block:", 6) == 0) {
    const char *pattern = rule_expr + 6;
    if (*pattern == '\0') return strdup("<pattern>");
    return NULL;
  }

  if (strncmp(rule_expr, "drop:", 5) == 0) {
    const char *pattern = rule_expr + 5;
    if (*pattern == '\0') return strdup("<pattern>");
    return NULL;
  }

  return NULL;
}

static void free_hints_cb(void *ptr) {
  free(ptr);
}

static void print_bin(const char *input, size_t len) {
  if (get_term_rowcol() == -1) return;
  size_t max_len = (gcols > 20) ? (gcols - 40) : gcols;
  size_t shown = 0;

  for (size_t i = 0; i < len; i++) {
    unsigned char b = (unsigned char)input[i];
    if (shown >= max_len) {
      printf(COLOR_WARN "...[truncated %zu bytes]" RESET, len - i);
      break;
    }

    switch (b) {
      case '\n': printf("\\n"); shown += 2; break;
      case '\r': printf("\\r"); shown += 2; break;
      case '\t': printf("\\t"); shown += 2; break;
      case '\\': printf("\\\\"); shown += 2; break;
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

static void print_rules(void) {
  if (bl->nrules == 0) {
    printf(COLOR_INFO "No rules loaded." RESET "\n");
    fflush(stdout);
    return;
  }

  for (int i = 0; i < bl->nrules; i++) {
    rule *r = bl->rules[i];
    printf(DIM "#%d " RESET, r->id);

    if (r->action == ACTION_REPLY) {
      printf("reply:");
      print_bin(r->pattern, (size_t)r->p_len);
      printf(":");
      if (r->reply_src == REPLY_SRC_FILE) {
        printf("@file=%s", r->r_file_path);
      } else {
        print_bin(r->response, (size_t)r->r_len);
      }
    } else if (r->action == ACTION_BLOCK) {
      printf("block:");
      print_bin(r->pattern, (size_t)r->p_len);
    } else if (r->action == ACTION_DROP) {
      printf("drop:");
      print_bin(r->pattern, (size_t)r->p_len);
    }
    printf("\n");
  }

  fflush(stdout);
}

static void print_help(void) {
  printf("Available commands\n");
  printf("  addrule <action>:<pattern>:[<response>] - add a new rule\n");
  printf("  del <rule_id> - delete a rule\n");
  printf("  lsrules - list current blacklist\n");
  printf("  clear - clear screen\n");
  printf("  exit - terminate proxy\n");
  printf("  help - show this help\n");
}

static void add_rule_cmd(const char *args) {
  char err[ERROR_LEN];
  rule *r = NULL;

  if (args == NULL || *args == '\0') {
    ERR("missing rule definition");
    return;
  }

  if (rule_parse_line(args, &r, err) == -1) {
    ERR("%s", err);
    return;
  }

  if (add_rule_to_bl(r) == -1) return;
  printf(COLOR_SUCCESS "[+] Rule successfully added" RESET "\n");
}

static void del_rule_cmd(const char *args) {
  char *end;
  long id;

  if (args == NULL || *args == '\0') {
    ERR("missing id");
    return;
  }

  errno = 0;
  id = strtol(args, &end, 10);
  if (errno == ERANGE || end == args || *end != '\0') {
    ERR("invalid id");
    return;
  }

  if (del_rule_by_id((int)id) == -1) {
    WARN("no rule found for id=%ld", id);
    return;
  }

  printf(COLOR_SUCCESS "[+] Rule successfully deleted" RESET "\n");
}

static void add_command(char *name, void (*callback)(void)) {
  if (commands_len >= MAX_COMMANDS) return;
  commands[commands_len].name = name;
  commands[commands_len].callback = callback;
  commands_len++;
}

static void cmd_add_wrapper(void) {}
static void cmd_del_wrapper(void) {}
static void cmd_lsrules_wrapper(void) { print_rules(); }
static void cmd_clear_wrapper(void) { linenoiseClearScreen(); }
static void cmd_exit_wrapper(void) { exit_app(); }
static void cmd_help_wrapper(void) { print_help(); }

static void exec_command_line(char *line) {
  char *cmd = strtok(line, " ");
  char *args = strtok(NULL, "");

  if (cmd == NULL) return;
  if (args != NULL) {
    while (*args == ' ') args++;
  }

  if (strcmp(cmd, "addrule") == 0) {
    add_rule_cmd(args);
    return;
  }
  if (strcmp(cmd, "del") == 0) {
    del_rule_cmd(args);
    return;
  }

  for (int i = 0; i < commands_len; i++) {
    if (strcmp(cmd, commands[i].name) == 0) {
      commands[i].callback();
      return;
    }
  }

  ERR("unknown command: %s", cmd);
}

static int start_linenoise_editor(void) {
  if (ln_started) return 0;
  if (linenoiseEditStart(&ln_state, STDIN_FILENO, STDOUT_FILENO, ln_buf, sizeof(ln_buf), "> ") == -1) {
    return -1;
  }
  ln_started = 1;
  return 0;
}

void commands_init(void) {
  if (ln_initialized) return;

  commands_len = 0;
  add_command("lsrules", cmd_lsrules_wrapper);
  add_command("clear", cmd_clear_wrapper);
  add_command("help", cmd_help_wrapper);
  add_command("exit", cmd_exit_wrapper);

  /* Keep placeholders for commands parsed with arguments. */
  add_command("addrule", cmd_add_wrapper);
  add_command("del", cmd_del_wrapper);

  linenoiseSetCompletionCallback(completion_cb);
  linenoiseSetHintsCallback(hints_cb);
  linenoiseSetFreeHintsCallback(free_hints_cb);
  linenoiseHistorySetMaxLen(HISTORY_LEN);
  linenoiseHistoryLoad(get_history_path());
  ln_initialized = 1;
  if (start_linenoise_editor() == -1) {
    ERR("linenoise init failed");
  }
}

int read_command(void) {
  char *line;

  if (!ln_initialized) commands_init();
  if (start_linenoise_editor() == -1) return -1;

  line = linenoiseEditFeed(&ln_state);
  if (line == linenoiseEditMore) return 0;

  linenoiseEditStop(&ln_state);
  ln_started = 0;

  if (line == NULL) {
    if (errno == EAGAIN) exit_app(); /* Ctrl-C */
    if (errno == EWOULDBLOCK || errno == ENOENT) {
      return start_linenoise_editor();
    }
    return -1;
  }

  if (*line != '\0') {
    linenoiseHistoryAdd(line);
    save_history_file();
    exec_command_line(line);
  }
  linenoiseFree(line);

  return start_linenoise_editor();
}

int set_raw_mode(int fd, int enable) {
  (void)fd;
  (void)enable;
  return 0;
}
