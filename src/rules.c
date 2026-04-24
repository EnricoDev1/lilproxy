#include "proxy/commands.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <proxy/rules.h>
#include <proxy/types.h>
#include <proxy/state.h>

/* Free a rule struct and its fields */
void rule_free(rule *r) {
  if(!r) return;
  free(r->pattern);
  free(r->response);
  free(r);
}

/*
  This function inits the blacklist struct, allocating needed space
  and setting default values. It also performs error handling.
*/
static void *init_blacklist(int initial_cap) {
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
  Returns len >= 0, -1 escape error, -2 too long.
*/
static int parse_bytestring(const char *src, char *dst, int cap) {
   int i = 0;
   while (*src) {
     if (i >= cap - 1) return -2;
 
     if (src[0] == '\\') {
       if (src[1] == '\0') return -1;
       switch (src[1]) {
         case 'n': dst[i++] = '\n'; src += 2; break;
         case 'r': dst[i++] = '\r'; src += 2; break;
         case 't': dst[i++] = '\t'; src += 2; break;
         case '\\': dst[i++] = '\\'; src += 2; break;
         case 'x': {
           if (!isxdigit(src[2]) || !isxdigit(src[3])) return -1;
           char hex[3] = { src[2], src[3], '\0' };
           dst[i++] = (char)strtol(hex, NULL, 16);
           src += 4;
           break;
         }
         default:
           return -1;
       }
     } else {
       dst[i++] = *src++;
     }
   }
   dst[i] = '\0';
   return i;
 }

/* This helper function return the corresponding ACTION_VALUE given an action string. */
static rule_action parse_action(const char *action) {
  if (strcmp(action, "reply") == 0) return ACTION_REPLY;
  if (strcmp(action, "block") == 0) return ACTION_BLOCK;
  if (strcmp(action, "drop") == 0) return ACTION_DROP;
  return ACTION_NONE;
}

/*
  This local function safe-copy the error string from msg into err.
  This is used to handle errors in different ways based on the caller of rule_parse_line.
*/
static void build_error(char *err, char *msg) {
  if (err == NULL) return;
  snprintf(err, ERROR_LEN, "%s", msg);
}

/*
  This function parses the stdin-readed line into the corresponding out_rule object.
  The input is expected to have the format: action:pattern:[response].
  On success: allocates and initialize the rule struct, storing the value in *out_rule and returning 0.
  On error:   writes the error message inside 'err', free allocated memory and returns -1.
*/
int rule_parse_line(const char *line, rule **out_rule, char *err) {
  char tmp[512];   
  char *action, *pattern, *response;
  rule *r;
  
  if (!line || !*line) { build_error(err, "empty rule"); return -1; }
  snprintf(tmp, sizeof(tmp), "%s", line);
  tmp[strcspn(tmp, "\n")] = '\0';

  action = strtok(tmp, ":");
  pattern = strtok(NULL, ":");
  response = strtok(NULL, "");

  if (!action) { build_error(err, "missing action"); return -1; }

  rule_action a = parse_action(action);
  if (a == ACTION_NONE) { build_error(err, "unknown action"); return -1; }
  if (a == ACTION_REPLY && (!response || !*response)) { build_error(err, "reply needs a response"); return -1; }

  if (!pattern || !*pattern) { build_error(err, "missing pattern"); return -1; }
  r = calloc(1, sizeof(*r));
  if (r == NULL) return -1;
  r->action = a;
  r->pattern = malloc(MAX_PATTERN_LEN);
  if (r->pattern == NULL) {return -1;}

  int plen = parse_bytestring(pattern, r->pattern, MAX_PATTERN_LEN);
  if (plen < 0) { rule_free(r); build_error(err, plen == -1 ? "invalid escape" : "pattern too long"); return -1; }
  r->p_len = plen;

  if (a == ACTION_REPLY) {
    r->response = malloc(MAX_RESPONSE_LEN);
    if (r->response == NULL) { rule_free(r); return -1; }

    int rlen = parse_bytestring(response, r->response, MAX_RESPONSE_LEN);
    if (rlen < 0) { rule_free(r); build_error(err, rlen == -1 ? "invalid escape" : "response too long"); return -1; }
    r->r_len = rlen;
  }
  
  *out_rule = r;  
  return 0;
}

/*
  This function reallocate space for rules inside the blacklist struct,
  doubling its capacity.
*/
static void reallocate_rules() {
  bl->capacity *= 2;
  rule **tmp = realloc(bl->rules, bl->capacity*sizeof(rule *));
  if (tmp == NULL) { perror("realloc bl->rules"); exit(1); }
  bl->rules = tmp;
}

/* This function append a given rule to the blacklist, allocating more space if needed. */
void add_rule_to_bl(rule *r) {
  if (bl->nrules == bl->capacity)
    reallocate_rules();
  bl->rules[bl->nrules++] = r;
}

int del_rule_by_id(int id) {
  for (int i = 0; i < bl->nrules; i++) {
    rule *r = bl->rules[i];
    if (r->id == id) {
      for (int j = i; j < bl->nrules-1; j++) {
        bl->rules[j] = bl->rules[j+1];
      }
      bl->nrules--;
      rule_free(r);
      return 0;
    }
  }
  return -1;
}

/*
  This function loads rules from config.txt file, filling up the global blacklist.
  It also checks for allocation error.
*/
void load_rules() {
  bl = init_blacklist(DEFAULT_INITIAL_CAP);
  
  FILE *fp = fopen(cfg->rules_file, "r");
  if (fp == NULL) {perror("Error while opening rules file"); exit(1);}
  
  /* Read rules from file and fill the blacklist */
  char line[MAX_PATTERN_LEN];
  char err[ERROR_LEN];
  int rcount = 0;
  
  while (fgets(line, MAX_PATTERN_LEN, fp)) {
    rule *r = NULL;
    
    if (rule_parse_line(line, &r, err) == -1) {
      ERR("error at %s:%d - %s\n", cfg->rules_file, rcount+1, err);
      exit(1);
    }
    r->id = rcount++;
    add_rule_to_bl(r);
  }
  fclose(fp);
}

/* Given a stream of data, return 0 if they do not violates the blacklist rules */
rule *check_rules(const unsigned char *buf, int len) {
  for (int i = 0; i < bl->nrules; i++) {
    if (bl->rules[i] == NULL) {
      exit(1);
    }
    if (memmem(buf, len, bl->rules[i]->pattern, bl->rules[i]->p_len)) {
      return bl->rules[i];
    }
  }
  return NULL;
}
