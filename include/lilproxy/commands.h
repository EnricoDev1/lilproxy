#ifndef _COMMAND_H
#define _COMMAND_H

#define CMD_MAX 256
#define HISTORY_LEN 30
#define MAX_COMMANDS 10

void commands_init();
int read_command();

#endif
