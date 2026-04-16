#ifndef PROXY_RULES_H
#define PROXY_RULES_H

#include "state.h"
#include "types.h"

int parse_bytestring(const char *src, char *dst, int maxlen);
void load_rules();
rule *check_rules(const unsigned char *buf, int len);
void *init_blacklist(int initial_cap);
rule_action parse_action(const char *action);
int rule_parse_line(const char *line, rule **out_rule, char *err);
rule *build_rule(char *action, char *pattern, char *reply);
void add_rule_to_bl(rule *r);
void save_rules_to_file();

#endif
