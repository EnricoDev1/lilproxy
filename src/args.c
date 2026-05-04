#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <lilproxy/args.h>
#include <lilproxy/types.h>
#include <lilproxy/state.h>

/* Safe parse port from string to int. */
static int parse_port(const char *port) {
  char *end;
  long val = strtol(port, &end, 10);

  if (*end != '\0' || errno == ERANGE || val < 0 || val > 65535) {
    ERR("Port must be a number between 0-65535");
    return -1;
  }
  return (int)val;
}

/* Parse command line args: target ADDRESS and PORT */
int parse_args(int argc, char *argv[]) {
  static struct option long_opt[] = {
    {"target-port", required_argument, 0, 't'},
    {"target-addr", required_argument, 0, 'a'},
    {"port", required_argument, 0, 'l'},
    {"rules-file", optional_argument, 0, 'r'},
    {0, 0, 0, 0}
  };
  
  int idx = 0;
  int opt;
  
  snprintf(cfg->rules_file, MAX_FILENAME_LEN, "%s", "rules.txt");
  while((opt = getopt_long(argc, argv, "t:a:l:", long_opt, &idx)) != -1) {
    switch(opt) {
      case 't':
        cfg->t_port = parse_port(optarg);
        if (cfg->t_port == -1) return -1;
        break;
      case 'a':
        snprintf(cfg->addr, ADDR_LEN, "%s", optarg);
        break;
      case 'l':
        cfg->l_port = parse_port(optarg);
        if (cfg->l_port == -1) return -1;
        break;
      case 'r':
        snprintf(cfg->rules_file, MAX_FILENAME_LEN, "%s", optarg);
        break;
      case '?':
        return -1;
      case ':':
        return -1;
      default:
        return -1;
    }
  }
  return 0;
}
