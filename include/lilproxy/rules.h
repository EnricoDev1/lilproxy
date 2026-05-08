#ifndef LILPROXY_RULES_H
#define LILPROXY_RULES_H

#include <time.h>

typedef enum _RULE_ACTION {
  ACTION_BLOCK,
  ACTION_REPLY,
  ACTION_DROP,
  ACTION_NONE
} rule_action;

typedef enum _REPLY_SRC {
  REPLY_SRC_INLINE = 0,
  REPLY_SRC_FILE
} reply_src_type;

typedef struct _RULE {
  int id;
  int p_len;
  int r_len;
  rule_action action;
  char *pattern;
  
  char *response;

  reply_src_type reply_src;
  char *r_file_path;
  time_t r_file_mtime;  
} rule;

typedef struct _BLACKLIST {
  rule **rules;
  int nrules;
  int capacity;
} blacklist;

int load_rules();
rule *check_rules(const unsigned char *buf, int len);
int rule_parse_line(const char *line, rule **out_rule, char *err);
int rule_reply_refresh(rule *r, char *err);
int add_rule_to_bl(rule *r);
void rule_free(rule *r);
int del_rule_by_id(int id);

#endif
