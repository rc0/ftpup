/* Do site upload */

#include <stdio.h>
#include <sys/types.h>

#include "ftp.h"
#include "invent.h"

static void set_subdir_unique(struct fnode *x, int to_what)/*{{{*/
{
  /* x is the subdir list of the parent. */
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    e->is_unique = to_what;
    if (e->is_dir) {
      set_subdir_unique((struct fnode *) &e->x.dir.next, to_what);
    }
  }
}
/*}}}*/

static void set_file_unique(struct fnode *x, int to_what)/*{{{*/
{
  x->is_unique = to_what;
  if (x->is_dir) set_subdir_unique((struct fnode *) &x->x.dir.next, to_what);
}
/*}}}*/
static void inner_reconcile(struct fnode *f1, struct fnode *f2)/*{{{*/
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
    set_file_unique(e1, 1);
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
        set_file_unique(e1, 1);
/*}}}*/
      }
    } else {
      if (e2->is_dir) {
/*{{{ file/dir */
        e1->is_unique = 1;
        set_file_unique(e2, 1);
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
/*}}}*/
static void reconcile(struct fnode *f1, struct fnode *f2)/*{{{*/
{
  /* Work out what's in each tree that's not in the other one. */
  set_subdir_unique(f1, 0);
  set_subdir_unique(f2, 1);
  inner_reconcile(f1, f2);
}
/*}}}*/
static void print_unique(struct fnode *x)/*{{{*/
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
/*}}}*/
static void upload_dummy(struct fnode *localinv, struct fnode *fileinv)/*{{{*/
{
  printf("UNIQUE IN LOCAL FILESYSTEM\n");
  print_unique(localinv);
  printf("\n\nUNIQUE IN REMOTE FILESYSTEM (FROM listing file)\n");
  print_unique(fileinv);
}
/*}}}*/
/* Assume already in correct local directory. */
int upload(const char *hostname, const char *username, const char *password, const char *remote_root, int is_dummy_run, const char *listing_file)/*{{{*/
{
  struct fnode *localinv;
  struct fnode *fileinv;

  fileinv = make_fileinv(listing_file);
  localinv = make_localinv();

#if 0
  printf("LOCAL INVENTORY:\n");
  print_inventory(localinv);
  printf("\n\nREMOTE INVENTORY:\n");
  print_inventory(fileinv);
#endif
  
  reconcile(fileinv, localinv);

  if (is_dummy_run) {
    upload_dummy(localinv, fileinv);
  } else {

  }

  return;
}
/*}}}*/

/* arch-tag: 33897a53-8f50-4ecb-802b-5ddfac6e4a95
*/
