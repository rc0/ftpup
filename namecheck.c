
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "memory.h"
#include "namecheck.h"

enum pattern_type {/*{{{*/
  PT_EXACT,
  PT_STARTS_WITH,
  PT_ENDS_WITH
};
/*}}}*/
struct pattern {/*{{{*/
  struct pattern *next;
  int pass; /* 1 for a positve pattern, 0 for a negative one */
  enum pattern_type type;
  char *pattern;
};
/*}}}*/
struct namecheck {/*{{{*/
  struct pattern *head;
  struct pattern *tail;
};
/*}}}*/
static void map_line(char *line, struct namecheck *nc)/*{{{*/
{
  char *p, *q;
  int len;
  int negate = 0;
  int wild_at_start = 0;
  int wild_at_end = 0;
  struct pattern *np;
  
  p = line;
  len = strlen(line);
  if (line[len-1] == '\n') line[len-1] = 0;
  
  /* Allow comments */
  if (*p == '#') return;

  if (*p == '!') {
    negate = 1;
    p++;
  }

  while (*p && isspace(*p)) p++;

  if (*p == '*') {
    wild_at_start = 1;
    p++;
  }

  if (*p) {
    q = p;
    while (*q) {
      if ((q[0] == '*') && !q[1]) {
        wild_at_end = 1;
        q[0] = 0;
        break;
      }
      q++;
    }
  }

  if (wild_at_start && wild_at_end) {
    fprintf(stderr, "Cannot have pattern that is wild at both start and end : [%s]\n", line);
    exit(1);
  }

  /* Now build the record. */ 
  np = new(struct pattern);
  if (wild_at_start) np->type = PT_ENDS_WITH;
  else if (wild_at_end) np->type = PT_STARTS_WITH;
  else np->type = PT_EXACT;

  if (p) {
    np->pattern = new_string(p);
  } else {
    np->pattern = NULL; /* total wildcard */
  }
  np->pass = negate ? 0 : 1;
  
  np->next = NULL;
  if (!nc->head) {
    nc->head = nc->tail = np;
  } else {
    nc->tail->next = np;
    nc->tail = np;
  }
  return;
}
/*}}}*/
/* Functions for comparing a filename against a list of patterns
 * and returning a boolean. */

struct namecheck *make_namecheck(char *filename)/*{{{*/
{
  struct namecheck *result;
  FILE *in;
  char line[1024];

  in = fopen(filename, "r");
  if (in) {
    result = new(struct namecheck);
    result->head = result->tail = NULL;
    while (fgets(line, sizeof(line), in)) {
      map_line(line, result);
    }
    fclose(in);
  } else {
    result = NULL;
  }

  return result;

}/*}}}*/
struct namecheck *make_namecheck_dir(const char *dir, const char *filename)/*{{{*/
{
  int l1, l2;
  char *temp;
  struct namecheck *result;

  l1 = strlen(dir);
  l2 = strlen(filename);
  temp = new_array(char, l1 + l2 + 2);
  strcpy(temp, dir);
  strcat(temp, "/");
  strcat(temp, filename);
  result = make_namecheck(temp);
  free(temp);
  return result;
}
/*}}}*/
void free_namecheck(struct namecheck *nc)/*{{{*/
{
  struct pattern *p, *np;
  for (p=nc->head; p; p=np) {
    np = p->next;
    free(p->pattern);
    free(p);
  }
  free(nc);
}
/*}}}*/

static int ends_with(const char *name, const char *pattern) {/*{{{*/
  int len, patlen;
  len = strlen(name);
  patlen = strlen(pattern);
  if (len > patlen) {
    if (!strcmp(name+len-patlen, pattern)) {
      return 1;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}
/*}}}*/
static int starts_with(const char *name, const char *pattern)/*{{{*/
{
  if (pattern) {
    int patlen = strlen(pattern);
    if (!strncmp(name, pattern, patlen)) {
      return 1;
    } else {
      return 0;
    }
  } else {
    /* Full wildcard, i.e. '*' or '!*' */
    return 1;
  }
}
/*}}}*/
enum nc_result lookup_namecheck(const struct namecheck *nc, const char *filename)/*{{{*/
{
  /* It's always the FIRST match that prevails - this is quicker than searching
   * for the last match! */
  struct pattern *p;
  for (p=nc->head; p; p=p->next) {
    switch (p->type) {
      case PT_EXACT:
        if (!strcmp(filename, p->pattern)) {
          if (p->pass) return NC_PASS;
          else         return NC_FAIL;
        }
        break;
      case PT_STARTS_WITH:
        if (starts_with(filename, p->pattern)) {
          if (p->pass) return NC_PASS;
          else         return NC_FAIL;
        }
        break;
      case PT_ENDS_WITH:
        if (ends_with(filename, p->pattern)) {
          if (p->pass) return NC_PASS;
          else         return NC_FAIL;
        }
        break;
    }
  }
  return NC_UNMATCHED;
}
/*}}}*/
/* arch-tag: b2462a14-b47e-4b6e-ba7f-722fc53e1ab3
 * */
