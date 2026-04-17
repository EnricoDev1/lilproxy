#ifndef _COMMAND_H
#define _COMMAND_H

#define CMD_MAX 256

struct cmdBuffer {
  char cmd[CMD_MAX];
  int len;
};

int read_command();
int set_raw_mode(int fd, int enable);

#endif
