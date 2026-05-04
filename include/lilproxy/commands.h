#ifndef _COMMAND_H
#define _COMMAND_H

#define CMD_MAX 256
#define HISTORY_LEN 30
#define CTRL_KEY(k) ((k) & 0x1f)
#define ARROW_KEY(k) ((k) >= ARROW_LEFT && (k) <= ARROW_DOWN)
#define PROMPT_CHAR ">"
#define MAX_COMMANDS 10

enum specialKeys {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY
};

struct cmdBuffer {
  char cmd[CMD_MAX];
  int len;
};

struct commandEntry {
  char *name;
  void (*callback)(void);
};

void commands_init();
int read_command();
int set_raw_mode(int fd, int enable);

#endif
