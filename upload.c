/* Do site upload */

#include <stdio.h>
#include <sys/types.h>

#include "ftp.h"
#include "invent.h"
#include "memory.h"

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
        /* Mark uniques == 0, so that the new file is uploaded 'in place'
         * rather than being deleted in the 1st pass and then uploaded anew in
         * the 2nd pass. */
        e1->is_unique = e2->is_unique = 0;
        e1->x.file.peer = e2;
        e2->x.file.peer = e1;
        if (e1->x.file.size == e2->x.file.size) {
          /* Further check based on mtime.  Treat zero mtime as a wildcard
           * (e.g. when the remote index has been built for the first time.)
           * */
          if (!e1->x.file.mtime || !e2->x.file.mtime ||
              (e1->x.file.mtime == e2->x.file.mtime)) {
            e1->x.file.is_stale = e2->x.file.is_stale = 0;
          } else {
            e1->x.file.is_stale = e2->x.file.is_stale = 1;
          }
        } else {
          /* Certainly differing */
          e1->x.file.is_stale = e2->x.file.is_stale = 1;
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
static void print_stale(struct fnode *x)/*{{{*/
{
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    if (!e->is_unique && !e->is_dir && e->x.file.is_stale) {
      printf("F %s\n", e->path);
    }
    if (e->is_dir) {
      print_stale((struct fnode *) &e->x.dir.next);
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
  printf("\n\nOUT OF DATE IN REMOTE FILESYSTEM (FROM listing file)\n");
  print_stale(fileinv);
}
/*}}}*/

static void remove_directory(struct FTP *ctrl_con, struct fnode *dir, FILE *journal)/*{{{*/
{
  int status;

  /* FIXME : create magic symlink to track aborted FTP ops */

  status = ftp_rmdir(ctrl_con, dir->path);
  if (status) {
    fprintf(journal, "Z %s\n", dir->path);
    fflush(journal);
    printf("Removed remote directory %s\n", dir->path);
    fflush(stdout);
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
    fflush(stdout);
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
    fflush(stdout);
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
  printf("Starting to create remote file %s (%d bytes)\r", file->path, file->x.file.size);
  fflush(stdout);
  status = ftp_write(ctrl_con, file->path, file->path);
  /* FIXME : md5sum */
  if (status) {
    fprintf(journal, "F %8d %08lx %s\n", file->x.file.size, file->x.file.mtime, file->path);
    fflush(journal);
    printf("Done creating new remote file %s (%d bytes)  \n", file->path, file->x.file.size);
    fflush(stdout);
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

static void update_file(struct FTP *ctrl_con, struct fnode *file, FILE *journal)/*{{{*/
{
  int status;
  struct fnode *local_peer = file->x.file.peer;
  /* FIXME : magic symlink */
  /* ? do we need to delete the file first for safety (according to STOR in
   * RFC959, no.) */
  printf("Starting to update %s (%d bytes)\r", file->path, local_peer->x.file.size);
  fflush(stdout);
  status = ftp_write(ctrl_con, file->path, file->path);
  /* FIXME : md5sum */
  if (status) {
    fprintf(journal, "F %8d %08lx %s\n", local_peer->x.file.size, local_peer->x.file.mtime, file->path);
    fflush(journal);
    printf("Done updating remote file %s (%d bytes)  \n", file->path, local_peer->x.file.size);
    fflush(stdout);
  } else {
    fprintf(stderr, "FAILED TO UPDATE FILE %s ON REMOTE SIZE, ABORTING\n", file->path);
    exit(1);
  }
}
/*}}}*/
static void update_stale_files(struct FTP *ctrl_con, struct fnode *fileinv, FILE *journal)/*{{{*/
{
  struct fnode *a;
  for (a = fileinv->next; a != fileinv; a = a->next) {
    /* deepest directories first */

    if (a->is_dir) {
      update_stale_files(ctrl_con, (struct fnode *) &a->x.dir.next, journal);
    } else {
      /* it's a file */
      if (a->x.file.is_stale) {
        update_file(ctrl_con, a, journal);
      } else {
        /* nothing to do. */
      }
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
  update_stale_files(ctrl_con, fileinv, journal);

  return;

}
/*}}}*/

void init_remote_params(struct remote_params *rp)/*{{{*/
{
  rp->hostname    = NULL;
  rp->username    = NULL;
  rp->remote_root = NULL;
}
/*}}}*/

static void preen_listing(const char *listing_file)/*{{{*/
{
  char *nlf;
  int len;
  struct remote_params rp;
  struct fnode *fileinv;

  len = strlen(listing_file);
  nlf = new_array(char, len + 5);
  strcpy(nlf, listing_file);
  strcat(nlf, ".new");

  init_remote_params(&rp);
  fileinv = make_fileinv(listing_file, &rp);
  print_inventory(fileinv, nlf, rp.hostname, rp.username, rp.remote_root);
  if (rename(nlf, listing_file) < 0) {
    fprintf(stderr, "Could not rename new listing file %s to %s\n", nlf, listing_file);
    exit(1);
  }

  free(rp.hostname);
  free(rp.username);
  if (rp.remote_root) free(rp.remote_root);
  free(nlf);
  /* FIXME : fileinv is leaked */

}
/*}}}*/

/* Assume already in correct local directory. */
int upload(const char *password, int is_dummy_run, const char *listing_file, int active_ftp)/*{{{*/
{
  struct fnode *localinv;
  struct fnode *fileinv;
  struct remote_params rp;

  if (!is_dummy_run) {
    printf("Preening listing file... "); fflush(stdout);
    preen_listing(listing_file);
    printf("done\n"); fflush(stdout);
  }

  init_remote_params(&rp);

  fileinv = make_fileinv(listing_file, &rp);
  localinv = make_localinv(listing_file);
  
  reconcile(fileinv, localinv);

  if (is_dummy_run) {
    upload_dummy(localinv, fileinv);
  } else {
    struct FTP *ctrl_con;
    ctrl_con = ftp_open(rp.hostname, rp.username, password, active_ftp);
    if (rp.remote_root) {
      ftp_cwd(ctrl_con, rp.remote_root);
    }
    ftp_binary(ctrl_con);
    upload_for_real(ctrl_con, localinv, fileinv, listing_file);
    ftp_close(ctrl_con);
  }

  return 0;
}
/*}}}*/

/* arch-tag: 33897a53-8f50-4ecb-802b-5ddfac6e4a95
*/
