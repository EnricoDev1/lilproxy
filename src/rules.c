#include <stdarg.h>
#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>

#include <lilproxy/rules.h>
#include <lilproxy/types.h>
#include <lilproxy/state.h>

static int next_rule_id = 0;

/* Free a rule struct and its fields */
void rule_free(rule *r) {
  if(!r) return;
  free(r->pattern);
  if (r->action == ACTION_REPLY) {
    free(r->response);
      if (r->reply_src == REPLY_SRC_FILE)
      free(r->r_file_path);
  }
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
    return NULL;
  }

  if (initial_cap <= 0) initial_cap = DEFAULT_INITIAL_CAP;

  bl->nrules = 0;
  bl->capacity = initial_cap;
  bl->rules = malloc((size_t)bl->capacity * sizeof(*bl->rules));
  if (bl->rules == NULL) {
    perror("malloc bl->rules");
    return NULL;
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
static void build_error(char *err, const char *fmt, ...) {
  if (err == NULL) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(err, ERROR_LEN, fmt, args);
  va_end(args);
}

/* checks if the rule provided file changed since last load, in case, it upload the response buffer with new contents */
int rule_reply_refresh(rule *r, char *err) {
  struct stat st;

  /* check if the action data is correct and if the file is ok */
  if (r->action != ACTION_REPLY || r->reply_src != REPLY_SRC_FILE) return 0;

  if (stat(r->r_file_path, &st) == -1) {
    build_error(err, "cannot stat %s", r->r_file_path);
    return -1;
  }

  /* file didn't changed */
  if (r->response && r->r_file_mtime == st.st_mtime) return 0; 

  FILE *fp = fopen(r->r_file_path, "rb");
  if (fp == NULL) {
    build_error(err, "cannot open %s", r->r_file_path);
    return -1;
  }

  // calculate file size
  if (fseek(fp, 0, SEEK_END) != 0) {
    build_error(err, "failed fseek on %s", r->r_file_path);
    fclose(fp);
    return -1;
  }

  long sz = ftell(fp);
  if (sz < 0 || sz > MAX_RESPONSE_LEN) {
    build_error(err, "invalid file size %s", r->r_file_path);
    fclose(fp);
    return -1;
  }
  rewind(fp);

  // read file contents 
  char *buf = malloc((size_t)sz + 1);
  if (buf == NULL) {
    build_error(err, "failed file buf malloc");
    fclose(fp);
    return -1;
  }
  
  if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
    build_error(err, "failed to read from file %s", r->r_file_path);
    free(buf);
    fclose(fp);
    return -1;
  }
  fclose(fp);
  buf[sz] = '\0';

  // in case everything went good, we update the rule data with the changed file ones
  free(r->response);
  r->response = buf;
  r->r_len = (int)sz;
  r->r_file_mtime = st.st_mtime;
  return 0; 
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
  
  /* parse the REPLY action type */
  if (a == ACTION_REPLY) {
    
    // parse the FILE based response type
    if (strlen(response) > 6 && strncmp(response, "@file=", 6) == 0) {
      r->reply_src = REPLY_SRC_FILE;
      r->r_file_path = strdup(response+6);

      if (!r->r_file_path || !*r->r_file_path) {
       rule_free(r); build_error(err, "invalid @file path"); return -1;
      }

      r->response = NULL;
      r->r_len = 0;
      r->r_file_mtime = 0; // force the first load into memory
    } else {
      // parse the INLINE reponse type
      r->reply_src = REPLY_SRC_INLINE;   
      r->response = malloc(MAX_RESPONSE_LEN);
      if (r->response == NULL) { rule_free(r); return -1; }

      int rlen = parse_bytestring(response, r->response, MAX_RESPONSE_LEN);
      if (rlen < 0) { rule_free(r); build_error(err, rlen == -1 ? "invalid escape" : "response too long"); return -1; }
      r->r_len = rlen;
    }
  }
  
  *out_rule = r;  
  return 0;
}

/*
  This function reallocate space for rules inside the blacklist struct,
  doubling its capacity.
*/
static int reallocate_rules() {
  bl->capacity *= 2;
  rule **tmp = realloc(bl->rules, bl->capacity*sizeof(rule *));
  if (tmp == NULL) { perror("realloc bl->rules"); return -1; }
  bl->rules = tmp;
  return 0;
}

/* This function append a given rule to the blacklist, allocating more space if needed. */
int add_rule_to_bl(rule *r) {
  if (bl->nrules == bl->capacity)
    if (reallocate_rules() == -1) { ERR("Failed to add rule" ); return -1;}
  r->id = next_rule_id++;
  bl->rules[bl->nrules++] = r;

  return 0;
}

/* delete a rule from the blacklist based on its id */
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
*/
int load_rules() {
  bl = init_blacklist(DEFAULT_INITIAL_CAP);
  if (bl == NULL) return -1;
  
  FILE *fp = fopen(cfg->rules_file, "r");
  if (fp == NULL) {
    WARN("no initial rules file provided.");
    return 0;
  }

  /* Read rules from file and fill the blacklist */
  char line[MAX_PATTERN_LEN];
  char err[ERROR_LEN];
  int lcount = 0;
  
  while (fgets(line, MAX_PATTERN_LEN, fp)) {
    rule *r = NULL;
    
    if (rule_parse_line(line, &r, err) == -1) {
      ERR("at %s:%d - %s", cfg->rules_file, lcount+1, err);
      fclose(fp);
      return -1;
    }
    if (add_rule_to_bl(r) == -1) {
      fclose(fp);
      return -1;
    }
    lcount++;
  }

  fclose(fp);
  return 0;
}

/* Given a stream of data, return 0 if they do not violates the blacklist rules */
rule *check_rules(const unsigned char *buf, int len) {
  for (int i = 0; i < bl->nrules; i++) {
    if (bl->rules[i] == NULL) {
      return NULL;
    }
    if (memmem(buf, len, bl->rules[i]->pattern, bl->rules[i]->p_len)) {
      return bl->rules[i];
    }
  }
  return NULL;
}
