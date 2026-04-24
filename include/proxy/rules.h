#ifndef PROXY_RULES_H
#define PROXY_RULES_H

#include "types.h"

typedef enum _RULE_ACTION {
  ACTION_BLOCK,
  ACTION_REPLY,
  ACTION_DROP,
  ACTION_NONE
} rule_action;

typedef struct _RULE {
  int id;
  int p_len;
  int r_len;
  rule_action action;
  char *pattern;
  char *response;
} rule;

typedef struct _BLACKLIST {
  rule **rules;
  int nrules;
  int capacity;
} blacklist;

void load_rules();
rule *check_rules(const unsigned char *buf, int len);
int rule_parse_line(const char *line, rule **out_rule, char *err);
void add_rule_to_bl(rule *r);
void rule_free(rule *r);
int del_rule_by_id(int id);

#endif
