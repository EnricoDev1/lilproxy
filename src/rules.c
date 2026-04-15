#include <unistd.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "rules.h"
#include "types.h"

/*
  This function inits the blacklist struct, allocating needed space
  and setting default values. It also performs error handling.
*/
void *init_blacklist(int initial_cap) {
  blacklist *bl = malloc(sizeof(*bl));
  if (bl == NULL) {
    perror("malloc bl");
    exit(1);
  }

  if (initial_cap <= 0) initial_cap = DEFAULT_INITIAL_CAP;

  bl->nrules = 0;
  bl->capacity = initial_cap;
  bl->rules = malloc((size_t)bl->capacity * sizeof(*bl->rules));
  if (bl->rules == NULL) {
    perror("malloc bl->rules");
    exit(1);
  }

  return bl;
}

/*
  Convert a string containing escape sequences into a byte string.
  \xBB -> 0xBB (single byte with value BB)
  \n -> 0x0a
  \r -> 0x0d
  \t -> 0x09
  \\ -> \
*/
int parse_bytestring(const char *src, char *dst, int maxlen) {
  int i = 0;
  while (*src && i < maxlen) {
    if (src[0] == '\\') {
      switch(src[1]) {
        case 'x': {
          char hex[3] = {src[2], src[3], '\0'};
          dst[i++] = (char)strtol(hex, NULL, 16);
          src += 4;
          break;
        }
        case 'n': dst[i++] = '\n'; src += 2; break;
        case 'r': dst[i++] = '\r'; src += 2; break;
        case 't': dst[i++] = '\t'; src += 2; break;
        case '\\': dst[i++] = '\\'; src += 2; break;
        default: dst[i++] = *src++; break;
      }
    } else {
      dst[i++] = *src++;
    }
  }
  return i;
}

/* Check if the rules provided by the config file are in the correct format */
int validate_rule(const char *action, const char *pattern, const char *response, int n_line) {
  if (action == NULL || action[0] == '\n') {
    fprintf(stderr, "config: missing action\n");
    return -1;
  }
  
  if (pattern == NULL || pattern[0] == '\0' || pattern[0] == '\n') {
    fprintf(stderr, "config: missing pattern at line %d\n", n_line);
    return -1;
  }
  
  if (strncmp(action, "reply", 5) == 0 && (response == NULL || response[0] == '\0' || response[0] == '\n')) {
    fprintf(stderr, "config: action=reply but no response set");
    return -1;
  }
  return 0;
}
/*
  This function reallocate space for rules inside the blacklist struct,
  doubling its capacity.
 */
void reallocate_rules() {
  bl->capacity *= 2;
  rule **tmp = realloc(bl->rules, bl->capacity*sizeof(rule *));
  if (tmp == NULL) { perror("realloc bl->rules"); exit(1); }
  bl->rules = tmp;
}

/* This helper function return the corresponding ACTION_VALUE given an action string. */
static rule_action parse_action(const char *action) {
  if (strcmp(action, "reply") == 0) return ACTION_REPLY;
  if (strcmp(action, "block") == 0) return ACTION_BLOCK;
  if (strcmp(action, "drop") == 0) return ACTION_DROP;
  
  fprintf(stderr, "config: unknown action %s\n", action);
  exit(1);
}

/*
  This function loads rules from config.txt file, filling up the global blacklist.
  It also checks for allocation error.
*/
void load_rules(const char *rules_file) {
  bl = init_blacklist(DEFAULT_INITIAL_CAP);

  FILE *fp = fopen(rules_file, "r");
  if (fp == NULL) {perror("Error while opening rules file"); exit(1);}

  /* Read rules from file and fill the blacklist */
  char line[MAX_PATTERN_LEN];
  int line_count = 1;
  
  while (fgets(line, MAX_PATTERN_LEN, fp)) {
    line[strcspn(line, "\n")] = '\0';
    char *action = strtok(line, ":");
    char *pattern = strtok(NULL, ":");
    char *response = strtok(NULL, "\n");
    if (validate_rule(action, pattern, response, line_count++) == -1) exit(1);

    if (bl->nrules == bl->capacity) 
      reallocate_rules();

    /* Create new rule and append it to blacklist. */
    rule *r = malloc(sizeof(*r));
    if (r == NULL) {perror("malloc rule"); exit(1);}
    
    /* Fill the pattern relative rule fields. */
    r->action = parse_action(action);

    r->pattern = malloc((255) * sizeof(char));
    if (r->pattern == NULL) {perror("malloc r->pattern"); exit(1);}
    r->len = parse_bytestring(pattern, r->pattern, MAX_PATTERN_LEN);

    if (r->action == ACTION_REPLY) {
      r->response = malloc((255) * sizeof(char));
      if (r->response == NULL) {perror("malloc r->response"); exit(1);}
      r->r_len = parse_bytestring(response, r->response, MAX_RESPONSE_LEN);
    }
    
    bl->rules[bl->nrules++] = r;
  }
  fclose(fp);
}

/*Given a stream of data, return 0 if they do not violates the blacklist rules */
rule *check_rules(const unsigned char *buf, int len) {
  for (int i = 0; i < bl->nrules; i++) {
    if (memmem(buf, len, bl->rules[i]->pattern, bl->rules[i]->len)) {
      return bl->rules[i];
    }
  }
  return NULL;
}
