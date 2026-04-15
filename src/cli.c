#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "cli.h"
#include "types.h"

/* Safe parse port from string to int. */
int parse_port(const char *port) {
  char *end;
  long val = strtol(port, &end, 10);

  if (*end != '\0' || errno == ERANGE || val < 0 || val > 65535) {
    fprintf(stderr, "Port must be a number between 0-65535");
    exit(1);
  }
  return (int)val;
}

/* Parse command line args: target ADDRESS and PORT */
void parse_args(int argc, char *argv[], char *addr, int *r_port, int *l_port, char *rules_file) {
  static struct option long_opt[] = {
    {"remote-port", required_argument, 0, 'p'},
    {"remote-addr", required_argument, 0, 'a'},
    {"port", required_argument, 0, 'l'},
    {"rules", optional_argument, 0, 'r'},
    {0, 0, 0, 0}
  };

  int idx = 0;
  int opt;
  
  snprintf(rules_file, MAX_FILENAME_LEN, "%s", "rules.txt");
  while((opt = getopt_long(argc, argv, "p:a:l:", long_opt, &idx)) != -1) {
    switch(opt) {
      case 'p':
        *r_port = parse_port(optarg);
        break;
      case 'a':
        snprintf(addr, ADDR_LEN, "%s", optarg);
        break;
      case 'l':
        *l_port = parse_port(optarg);
        break;
      case 'r':
        snprintf(rules_file, MAX_FILENAME_LEN, "%s", optarg);
        break;
      case '?':
        exit(1);
      case ':':
        exit(1);
      default:
        exit(1);
    }
  }
}
