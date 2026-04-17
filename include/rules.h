#ifndef PROXY_RULES_H
#define PROXY_RULES_H

#include "state.h"
#include "types.h"

void load_rules();
rule *check_rules(const unsigned char *buf, int len);
int rule_parse_line(const char *line, rule **out_rule, char *err);
void add_rule_to_bl(rule *r);

#endif
