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
   X - indeterminate

   The last match for given filename wins.  This allows appending to the end of
   the file as remote files are updated, giving an auto-journalling capability.

   If the last entry for a file is 'X', it means a previous update operation
   was botched, and the state of the remote file is unknown.  For now, the user
   will have to fix such problems by hand.

   For 'F' lines, the syntax is

   F <size> <mtime> <name>

   <name> is relative to the top directory, i.e. no / at the start.
   <size> in bytes
   <mtime> as an integer.

   D <name>

   X <name>

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

  p = line+1;
  while (isspace(*p)) p++;
  size = atol(p);
  while (!isspace(*p)) p++;
  while (isspace(*p)) p++;
  mtime = atol(p);
  while (!isspace(*p)) p++;
  while (isspace(*p)) p++;
  /* p now pointing to path */
  lookup_dir(a, p, p, &d, &tail);

  /* lookup */
  for (e = d->next; e != d; e = e->next) {
    if (!strcmp(e->name, tail)) {
      fprintf(stderr, "In add_file for %s, this file already exists.\n", p);
      exit(1);
    }
  }

  /* add */
  nfn = new(struct fnode);
  nfn->name = new_string(tail);
  nfn->path = new_string(p);
  nfn->is_indeterminate = 0;
  nfn->is_dir = 0;
  nfn->x.file.size = size;
  nfn->x.file.mtime = mtime;
  nfn->next = (struct fnode *) &d->next;
  nfn->prev = d->prev;
  d->prev->next = nfn;
  d->prev = nfn;
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
  nfn = new(struct fnode);
  nfn->name = new_string(tail);
  nfn->path = new_string(p);
  nfn->is_indeterminate = 0;
  nfn->is_dir = 1;
  nfn->x.dir.next = (struct fnode *) &nfn->x.dir;
  nfn->x.dir.prev = (struct fnode *) &nfn->x.dir;
  nfn->next = (struct fnode *) &d->next;
  nfn->prev = d->prev;
  d->prev->next = nfn;
  d->prev = nfn;
}
/*}}}*/
static void add_indeterminate(struct fnode *a, const char *line)/*{{{*/
{
  fprintf(stderr, "add_indeterminate not done yet\n");
  exit(1);
}
/*}}}*/
static void delete_entry(struct fnode *a, const char *line)/*{{{*/
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

struct fnode *make_remoteinv(const char *listing)/*{{{*/
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
      case 'F':
        add_file(result, line);
        break;
      case 'D':
        add_directory(result, line);
        break;
      case 'X':
        add_indeterminate(result, line);
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

static void set_unique(struct fnode *x, int to_what)/*{{{*/
{
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    e->is_unique = to_what;
    if (e->is_dir) {
      set_unique((struct fnode *) &e->x.dir.next, to_what);
    }
  }
}
/*}}}*/

static void inner_reconcile(struct fnode *f1, struct fnode *f2)
{
  struct fnode *e1;
  struct fnode *e2;

  for (e1 = f1->next; e1 != f1; e1 = e1->next) {
    for (e2 = f2->next; e2 != f2; e2 = e2->next) {
      if (!strcmp(e1->name, e2->name)) {
        goto matched;
      }
    }
    /* Not matched : e1 is unique (+ everything under it if it's a directory).
    */
    set_unique(e1, 1);
    continue;

matched:
    if (e1->is_dir) {
      if (e2->is_dir) {
/*{{{ dir/dir */
        e1->is_unique = e2->is_unique = 0;
        inner_reconcile((struct fnode *) &e1->x.dir.next,
                        (struct fnode *) &e2->x.dir.next);
/*}}}*/
      } else {
/*{{{ dir/file */
        e2->is_unique = 1;
        set_unique(e1, 1);
/*}}}*/
      }
    } else {
      if (e2->is_dir) {
/*{{{ file/dir */
        e1->is_unique = 1;
        set_unique(e2, 1);
/*}}}*/
      } else {
        /*{{{ file/file */
        /* FIXME : do something about mtime!! */
        if (e1->x.file.size == e2->x.file.size) {
          e1->is_unique = e2->is_unique = 0;
        } else {
          /* same name, different content. */
          e1->is_unique = e2->is_unique = 1;
        }
/*}}}*/
      }
    }
  }
}


static void reconcile(struct fnode *f1, struct fnode *f2)
{
  /* Work out what's in each tree that's not in the other one. */
  set_unique(f1, 0);
  set_unique(f2, 1);
  inner_reconcile(f1, f2);
}

static void print_unique(struct fnode *x)
{
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    if (e->is_unique) {
      printf("%c %s\n", e->is_dir ? 'D' : 'F',
             e->path);
    }
    if (e->is_dir) {
      print_unique((struct fnode *) &e->x.dir.next);
    }
  }
}

#ifdef TEST
int main (int argc, char **argv) {
  struct fnode *remoteinv;
  struct fnode *localinv;

  if (argc > 1) {
    remoteinv = make_remoteinv(argv[1]);
  } else {
    remoteinv = make_remoteinv("listing");
  }
  localinv  = make_localinv();
  reconcile (remoteinv, localinv);
  printf("UNIQUE IN LOCAL FILESYSTEM\n");
  print_unique(localinv);
  printf("\n\nUNIQUE IN REMOTE FILESYSTEM\n");
  print_unique(remoteinv);

#if 0
  print_inventory(remoteinv);
#endif
  return 0;
}
#endif

/* arch-tag: a2bd7446-7fee-4bd3-b1d2-db4e448b53a8
*/

