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

static void remove_directory(struct FTP *ctrl_con, struct fnode *dir, FILE *journal)/*{{{*/
{
  int status;

  /* FIXME : create magic symlink to track aborted FTP ops */
  if ((struct fnode *) &dir->x.dir.next != dir->x.dir.next) {
    fprintf(stderr, "Botched invariant for %s in remove_directory\n", dir->path);
    exit(2);
  }

  status = ftp_rmdir(ctrl_con, dir->path);
  if (status) {
    fprintf(journal, "Z %s\n", dir->path);
    fflush(journal);
    printf("Removed remote directory %s\n", dir->path);
  } else {
    fprintf(stderr, "FAILED TO REMOVE DIRECTORY %s FROM REMOTE SIZE, ABORTING\n", dir->path);
    exit(1);
  }
}
/*}}}*/
static void remove_file(struct FTP *ctrl_con, struct fnode *file, FILE *journal)/*{{{*/
{
  int status;
  /* FIXME : create magic symlink to track aborted FTP ops */
  status = ftp_delete(ctrl_con, file->path);
  if (status) {
    fprintf(journal, "Z %s\n", file->path);
    fflush(journal);
    printf("Removed remote file %s\n", file->path);
  } else {
    fprintf(stderr, "FAILED TO REMOVE FILE %s FROM REMOTE SIZE, ABORTING\n", file->path);
    exit(1);
  }
}
/*}}}*/

static void remove_dead_files(struct FTP *ctrl_con, struct fnode *fileinv, FILE *journal)/*{{{*/
{
  /* Start from the deepest directory. */
  struct fnode *a, *next_a;
  for (a = fileinv->next; a != fileinv; a = next_a) {
    if (a->is_dir) {
      remove_dead_files(ctrl_con, (struct fnode *) &a->x.dir.next, journal);
    }
    if (a->is_unique) {
      if (a->is_dir) {
        remove_directory(ctrl_con, a, journal);
      } else {
        remove_file(ctrl_con, a, journal);
      }
    }
    /* Do it this way so we can potentially handle freeing a from the list. */
    next_a = a->next;
  }

}
/*}}}*/

static void create_directory(struct FTP *ctrl_con, struct fnode *dir, FILE *journal)/*{{{*/
{
  int status;
  /* FIXME : magic symlink */
  status = ftp_mkdir(ctrl_con, dir->path);
  if (status) {
    fprintf(journal, "D                   %s\n", dir->path);
    fflush(journal);
    printf("Created new remote directory %s\n", dir->path);
  } else {
    fprintf(stderr, "FAILED TO CREATE DIRECTORY %s ON REMOTE SIZE, ABORTING\n", dir->path);
    exit(1);
  }
}
/*}}}*/
static void create_file(struct FTP *ctrl_con, struct fnode *file, FILE *journal)/*{{{*/
{
  int status;
  /* FIXME : magic symlink */
  status = ftp_write(ctrl_con, file->path, file->path);
  /* FIXME : md5sum */
  if (status) {
    fprintf(journal, "F %8d %08lx %s\n", file->x.file.size, file->x.file.mtime, file->path);
    fflush(journal);
    printf("Created new remote file %s\n", file->path);
  } else {
    fprintf(stderr, "FAILED TO CREATE FILE %s ON REMOTE SIZE, ABORTING\n", file->path);
    exit(1);
  }
}
/*}}}*/
static void add_new_files(struct FTP *ctrl_con, struct fnode *localinv, FILE *journal)/*{{{*/
{
  struct fnode *a;
  for (a = localinv->next; a != localinv; a = a->next) {
    /* Start with shallowest stuff */
    if (a->is_unique) {
      if (a->is_dir) {
        create_directory(ctrl_con, a, journal);
      } else {
        create_file(ctrl_con, a, journal);
      }
    }
    if (a->is_dir) {
      add_new_files(ctrl_con, (struct fnode *) &a->x.dir.next, journal);
    }
  }
}
/*}}}*/

static void upload_for_real(struct FTP *ctrl_con, struct fnode *localinv, struct fnode *fileinv, const char *listing_file)/*{{{*/
{
  FILE *journal;

  journal = fopen(listing_file, "a");
  if (!journal) {
    fprintf(stderr, "Couldn't open %s to append updates\n", listing_file);
    exit(1);
  }

  remove_dead_files(ctrl_con, fileinv, journal);
  add_new_files(ctrl_con, localinv, journal);
#if 0
  update_stale_files(ctrl_con, fileinv, journal);
#endif

  return;

}
/*}}}*/

/* Assume already in correct local directory. */
int upload(const char *hostname, const char *username, const char *password, const char *remote_root, int is_dummy_run, const char *listing_file)/*{{{*/
{
  struct fnode *localinv;
  struct fnode *fileinv;

  fileinv = make_fileinv(listing_file);
  localinv = make_localinv(listing_file);

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
    struct FTP *ctrl_con;
    ctrl_con = ftp_open(hostname, username, password);
    upload_for_real(ctrl_con, localinv, fileinv, listing_file);
    ftp_close(ctrl_con);
  }

  return;
}
/*}}}*/

/* arch-tag: 33897a53-8f50-4ecb-802b-5ddfac6e4a95
*/
