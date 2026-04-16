#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rules.h"
#include "types.h"

void print_rules() {
  for (int i = 0; i < bl->nrules; i++) {
    rule *r = bl->rules[i];
    if (r->action == ACTION_REPLY) printf("reply:%s:%s\n", r->pattern, r->response);
    else if(r->action == ACTION_BLOCK) printf("block:%s\n", r->pattern);
    else if(r->action == ACTION_DROP) printf("drop:%s\n", r->pattern);
  } 
}

void clear_screen() {
  puts("\x1b[H\x1b[2J\x1b[3J");
}

int read_command() {
  char cmd[256];

  if (fgets(cmd, sizeof(cmd), stdin) == NULL) return -1;
  cmd[strcspn(cmd, "\n")] = '\0';

  if (strncmp(cmd, "/addrule ", 9) == 0) {
    char *args = cmd+9;
    rule *r = NULL;
    char err[ERROR_LEN];
    
    int check = rule_parse_line(args, &r, err);
    if (check == -1) { ERR("error: %s", err); return -1; }

    add_rule_to_bl(r);
    printf("[*] Rule successfully added\n");
  }

  if (strcmp(cmd, "/lsrules") == 0) {
    print_rules();
  }

  if (strcmp(cmd, "/clear") == 0) {
    clear_screen();
  }
  return 0;
}
