/*
 * Handle the remote inventory.
 * */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "invent.h"
#include "memory.h"

/* Notes on listing file format.
 
   1st column indicates file type:
   F - ordinary file
   D - directory
   Z - deleted

   or the 3 special entries that only occur once:
   H <hostname>
   U <username>
   R <remote_root>

   The last match for given filename wins.  This allows appending to the end of
   the file as remote files are updated, giving an auto-journalling capability.

   For 'F' lines, the syntax is

   F <size> <mtime> <name>

   <name> is relative to the top directory, i.e. no / at the start.
   <size> in bytes
   <mtime> as an integer.

   D <name>

   Z <name>

   */

static void lookup_dir(struct fnode *top, const char *full_path, const char *path, struct fnode **dir, const char **tail)/*{{{*/
{
  const char *slash;
  slash = strchr(path, '/');
  if (slash) {
    int len = slash - path;
    struct fnode *e;
    for (e = top->next; e != top; e = e->next) {
      if (!strncmp(e->name, path, len)) {
        /* match */
        if (e->is_dir) {
          lookup_dir((struct fnode *) &e->x.dir.next, full_path, slash+1, dir, tail);
          return;
        } else {
          fprintf(stderr, "Found file instead of directory in lookup_dir for %s\n", full_path);
          exit(1);
        }
      }
    }
    fprintf(stderr, "Failed to find entry in lookup_dir for %s\n", full_path);
    exit(1);
  } else {
    /* final component */
    *dir = top;
    *tail = path;
    return;
  }
}
/*}}}*/
static void add_file(struct fnode *a, const char *line)/*{{{*/
{
  size_t size;
  time_t mtime;
  const char *p;
  struct fnode *d;
  const char *tail;
  struct fnode *e;
  struct fnode *nfn;
  char md5[16], buf[3];
  int i, val;
  int md5_defined;

  p = line+1;
  while (isspace(*p)) p++;
  size = atol(p);
  while (!isspace(*p)) p++;
  while (isspace(*p)) p++;
  sscanf(p, "%lx", &mtime);
  while (!isspace(*p)) p++;
  while (isspace(*p)) p++;

  /* p now pointing to md5 */
  if (*p == '?') {
    md5_defined = 0;
  } else {
    buf[2] = '\0';
    for (i=0; i<16; i++) {
      buf[0] = p[2*i];
      buf[1] = p[2*i+1];
      sscanf(buf, "%x", &val);
      md5[i] = (unsigned char) val;
    }
    md5_defined = 1;
  }

  while (!isspace(*p)) p++;
  while (isspace(*p)) p++;
  
  /* p now pointing to path */
  lookup_dir(a, p, p, &d, &tail);

  /* lookup */
  for (e = d->next; e != d; e = e->next) {
    if (!strcmp(e->name, tail)) {
      /* Update parameters */
      e->x.file.size = size;
      e->x.file.mtime = mtime;
      return;
    }
  }

  /* otherwise, add new entry */
  nfn = make_file_node(tail,p,size,mtime);
  nfn->x.file.md5_defined = md5_defined;
  if (md5_defined) {
    memcpy(nfn->x.file.md5, md5, 16);
  }
  add_fnode_at_end(d, nfn);
}
/*}}}*/
static void add_directory(struct fnode *a, const char *line)/*{{{*/
{
  const char *p;
  struct fnode *d;
  const char *tail;
  struct fnode *e;
  struct fnode *nfn;

  p = line+1;
  while (isspace(*p)) p++;
  /* p now pointing to path */
  lookup_dir(a, p, p, &d, &tail);

  /* lookup */
  for (e = d->next; e != d; e = e->next) {
    if (!strcmp(e->name, tail)) {
      fprintf(stderr, "In add_directory for %s, this file already exists.\n", p);
      exit(1);
    }
  }

  /* add */
  nfn = make_dir_node(tail,p);
  add_fnode_at_start(d, nfn);
}
/*}}}*/
static void delete_entry(struct fnode *a, const char *line)/*{{{*/
{
  const char *p;
  struct fnode *d;
  const char *tail;
  struct fnode *e;

  p = line+1;
  while (isspace(*p)) p++;
  /* p now pointing to path */
  lookup_dir(a, p, p, &d, &tail);

  /* lookup */
  for (e = d->next; e != d; e = e->next) {
    if (!strcmp(e->name, tail)) {
      goto do_delete;
    }
  }

  fprintf(stderr, "In delete_entry for %s, it doesn't exist in the database\n", p);
  exit(1);

do_delete:
  if (e->is_dir) {
    if (e->x.dir.next != (struct fnode *) &e->x.dir.next) {
      fprintf(stderr, "In delete_entry for %s, it's a non-empty directory\n", p);
      exit(1);
    } else {
      free(e->name);
      free(e->path);
      e->next->prev = e->prev;
      e->prev->next = e->next;
      free(e);
    }
  } else {
    /* file */
    free(e->name);
    free(e->path);
    e->next->prev = e->prev;
    e->prev->next = e->next;
    free(e);
  }
}
/*}}}*/

static char *copy_data(const char *in)/*{{{*/
{
  return new_string(in + 2);
}
/*}}}*/
struct fnode *make_fileinv(const char *listing, struct remote_params *rp)/*{{{*/
{
  /* listing is the path to the file containing the cache of what's on the
   * remote site. */

  FILE *in;
  char line[4096];
  int number;
  struct fnode *result;

  in = fopen(listing, "r");
  if (!in) {
    fprintf(stderr, "Couldn't open listing file %s\n", listing);
    exit(1);
  }

  number = 1;
  result = new(struct fnode);
  result->next = result->prev = result;

  while (fgets(line, sizeof(line), in)) {
    char *p;
    for (p=line; *p; p++) ;
    while (isspace(*--p)) {
      *p = '\0';
    }
    switch (line[0]) {
      case 'H':
        rp->hostname = copy_data(line);
        break;
      case 'U':
        rp->username = copy_data(line);
        break;
      case 'R':
        rp->remote_root = copy_data(line);
        break;
      case 'F':
        add_file(result, line);
        break;
      case 'D':
        add_directory(result, line);
        break;
      case 'Z':
        delete_entry(result, line);
        break;
      default:
        fprintf(stderr, "Line %d in listing file %s corrupted\n", number, listing);
        break;
    }
    number++;
  }
        
  fclose(in);
  return result;
}
/*}}}*/
#ifdef TEST
int main (int argc, char **argv) {
  struct fnode *fileinv;
  struct fnode *localinv;

  if (argc > 1) {
    fileinv = make_fileinv(argv[1]);
  } else {
    fileinv = make_fileinv("listing");
  }
  localinv  = make_localinv();
  reconcile (fileinv, localinv);

  return 0;
}
#endif

/* arch-tag: a2bd7446-7fee-4bd3-b1d2-db4e448b53a8
*/

