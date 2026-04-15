#ifndef PROXY_CLI_H
#define PROXY_CLI_H

#include "types.h"

int parse_port(const char *port);
void parse_args(int argc, char *argv[], char *addr, int *r_port, int *l_port, char *rules_file);

#endif
