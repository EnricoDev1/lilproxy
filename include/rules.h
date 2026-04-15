#ifndef PROXY_RULES_H
#define PROXY_RULES_H

#include "state.h"
#include "types.h"

int parse_bytestring(const char *src, char *dst, int maxlen);
int validate_rule(const char *action, const char *pattern, const char *response, int n_line);
void load_rules(const char *rules_file);
rule *check_rules(const unsigned char *buf, int len);
void *init_blacklist(int initial_cap);

#endif
