/* Do site upload */

#include <stdio.h>
#include <sys/types.h>
#include <assert.h>

#include "md5.h"
#include "ftp.h"
#include "invent.h"
#include "memory.h"

static void compute_file_md5sum(struct fnode *f)/*{{{*/
{
  MD5_CTX ctx;
  FILE *in;
  unsigned char buf[4096];
  int n;
  MD5Init(&ctx);
  in = fopen(f->path, "rb");
  if (!in) {
    fprintf(stderr, "Cannot open %s for reading\n", f->path);
    exit(1);
  }
  while (1) {
    n = fread(buf, 1, 4096, in);
    if (!n) break;
    MD5Update(&ctx, buf, n);
  }
  fclose(in);
  MD5Final(&ctx);
  memcpy(f->x.file.md5, ctx.digest, 16);
  f->x.file.md5_defined = 1;
}
/*}}}*/
static void compute_md5sums(struct fnode *d)/*{{{*/
{
  /* Only applied to the localinv tree */
  struct fnode *a;
  for (a = d->next; a != d; a = a->next) {
    if (a->is_dir) {
      compute_md5sums((struct fnode *) &a->x.dir.next);
    } else {
      /* unique or stale local file */
      if ((a->path_peer == NULL) || (a->x.file.content_peer == NULL)) {
        compute_file_md5sum(a);
      }
    }
  }
}
/*}}}*/
static void inner_reconcile(struct fnode *fileinv, struct fnode *localinv)/*{{{*/
{
  struct fnode *e1;
  struct fnode *e2;

  for (e1 = fileinv->next; e1 != fileinv; e1 = e1->next) {
    for (e2 = localinv->next; e2 != localinv; e2 = e2->next) {
      if (!strcmp(e1->name, e2->name)) {
        goto matched;
      }
    }
    /* Not matched : e1 is unique (+ everything under it if it's a directory).
    */
    continue;

matched:
    e1->path_peer = e2;
    e2->path_peer = e1;
    if (e1->is_dir) {
      if (e2->is_dir) {
/*{{{ dir/dir */
        inner_reconcile((struct fnode *) &e1->x.dir.next,
                        (struct fnode *) &e2->x.dir.next);
/*}}}*/
      } else {
/*{{{ dir/file */
/*}}}*/
      }
    } else {
      if (e2->is_dir) {
/*{{{ file/dir */
/*}}}*/
      } else {
        /*{{{ file/file */
        /* Mark uniques == 0, so that the new file is uploaded 'in place'
         * rather than being deleted in the 1st pass and then uploaded anew in
         * the 2nd pass. */
        if (e1->x.file.size == e2->x.file.size) {
          /* Further check based on mtime.  Treat zero mtime as a wildcard
           * (e.g. when the remote index has been built for the first time.)
           * */

          /* Don't do md5 checksum compares at this stage : that would require
           * md5summing every file in the local copy, which is too expensive.
           * */

          if (!e1->x.file.mtime || !e2->x.file.mtime ||
              (e1->x.file.mtime == e2->x.file.mtime)) {
            e1->x.file.content_peer = e2;
            e2->x.file.content_peer = e1;
          }
        } else {
        }
/*}}}*/
      }
    }
  }
}
/*}}}*/

static int count_unmatched(struct fnode *x)/*{{{*/
{
  /* Return number of remote files that don't already match (i.e. against their
   * path peer at this stage) and which have known md5 values. */
  int count = 0;
  struct fnode *a;
  for (a = x->next; a != x; a = a->next) {
    if (a->is_dir) {
      count += count_unmatched(dir_children(a));
    } else {
      if (!a->x.file.content_peer && a->x.file.md5_defined) {
        count++;
      }
    }
  }
  return count;
}
/*}}}*/
static void populate_unmatched(struct fnode *x, struct fnode **array, int *index)/*{{{*/
{
  struct fnode *a;
  for (a = x->next; a != x; a = a->next) {
    if (a->is_dir) {
      populate_unmatched(dir_children(a), array, index);
    } else {
      if (!a->x.file.content_peer && a->x.file.md5_defined) {
        array[*index] = a;
        ++*index;
      }
    }
  }
}
/*}}}*/
static void find_unmatched_files(struct fnode *fileinv, struct fnode ***entries, int *n_entries)/*{{{*/
{
  int N, index;
  struct fnode **ent;
  *n_entries = N = count_unmatched(fileinv);
  *entries = ent = new_array(struct fnode *, N);
  index = 0;
  populate_unmatched(fileinv, ent, &index);

#if 0
  /* Test */
  {
    int i;
    printf("Unmatched entries for potential rename :\n");
    for (i=0; i<N; i++) {
      printf("%s\n", ent[i]->path);
    }
  }
#endif
}
/*}}}*/

#define TMAP(T, a, aa) const T *aa = (const T *) (a)

static int compare_unmatched(const void *a, const void *b)/*{{{*/
{
  TMAP(struct fnode *, a, aa);
  TMAP(struct fnode *, b, bb);
  return strcmp((*aa)->x.file.md5, (*bb)->x.file.md5);
}
/*}}}*/

static void match_on_md5(struct fnode *a, struct fnode **table, int *n_table)/*{{{*/
{
  int l, h, m;
  int c;
  int hit = 0;
  int i;

  l = 0;
  h = *n_table;
  while (l < h) {
    m = (l + h) >> 1;
    c = strcmp(a->x.file.md5, table[m]->x.file.md5);
    if (c == 0) {
      hit = 1;
      break;
    }
    if (m == l) break;

    if (c < 0) h = m;
    else       l = m;

  }

  if (hit) {
    printf("Found a rename : local <%s> -> remote <%s>\n",
           a->path, table[m]->path);
    a->x.file.content_peer = table[m];
    table[m]->x.file.content_peer = a;
    /* Squeeze the matched entry out of the remote unmatched array.  This
     * prevents us getting multiple matches on the same remote file when
     * several local files somehow have identical parameters. */
    for (i = m+1; i < *n_table; i++) {
      table[i-1] = table[i];
    }
    --*n_table;
  } else {
#if 0
    char *md5buf = format_md5(a);
    printf("No rename match for %s (%s)\n", a->path, md5buf);
#endif
  }
}
/*}}}*/
static void matchup_files(struct fnode *lx, struct fnode **table, int *n_table)/*{{{*/
{
  struct fnode *a;
  for (a = lx->next; a != lx; a = a->next) {
    if (a->is_dir) {
      matchup_files(dir_children(a), table, n_table);
    } else {
      if (!a->x.file.content_peer) {
        /* Candidate for rename */
        if (!a->x.file.md5_defined) {
          compute_file_md5sum(a);
        }
        match_on_md5(a, table, n_table);
      }
    }
  }
}
/*}}}*/
static void find_content_matches(struct fnode *fileinv, struct fnode *localinv)/*{{{*/
{
  struct fnode **remote_unmatched;
  int n_remote_unmatched;

  find_unmatched_files(fileinv, &remote_unmatched, &n_remote_unmatched);
  qsort(remote_unmatched, n_remote_unmatched, sizeof(struct fnode *), compare_unmatched);
#if 0
  {
    int i;
    printf("After sorting\n");
    fflush(stdout);
    for (i=0; i<n_remote_unmatched; i++) {
      char *md5buf;
      md5buf = format_md5(remote_unmatched[i]);
      printf("%s %s\n", md5buf, remote_unmatched[i]->path);
    }
    printf("\n");
  }
#endif
  matchup_files(localinv, remote_unmatched, &n_remote_unmatched);

  free(remote_unmatched);
}
/*}}}*/
static int detect_local_dir_remote_file(struct fnode *x)/*{{{*/
{
  struct fnode *a;
  int fail = 0;
  for (a = x->next; a != x; a = a->next) {
    if (a->is_dir) {
      if (a->path_peer && !a->path_peer->is_dir) {
        fprintf(stderr, "Local dir %s is a remote file\n", a->path);
        fail = 1;
      }
      fail |= detect_local_dir_remote_file(dir_children(a));
    }
  }
  return fail;
}
/*}}}*/
static int detect_local_file_remote_dir(struct fnode *x)/*{{{*/
{
  struct fnode *a;
  int fail = 0;
  for (a = x->next; a != x; a = a->next) {
    if (a->is_dir) {
      fail |= detect_local_file_remote_dir(dir_children(a));
    } else {
      if (a->path_peer && a->path_peer->is_dir) {
        fprintf(stderr, "Local file %s is a remote dir\n", a->path);
        fail = 1;
      }
    }
  }
  return fail;
}
/*}}}*/
static int detect_dual_rename(struct fnode *x)/*{{{*/
{
  struct fnode *a;
  int fail = 0;
  for (a = x->next; a != x; a = a->next) {
    if (a->is_dir) {
      fail |= detect_dual_rename(dir_children(a));
    } else {
      if ((a->path_peer != NULL) && 
          (a->x.file.content_peer != NULL) &&
          (a->path_peer != a->x.file.content_peer) &&
          (!a->path_peer->is_dir) &&
          (a->path_peer->x.file.content_peer)) {
        fprintf(stderr, "Local file %s is in a rename chain with remote file %s\n",
            a->path, a->x.file.content_peer->path);
        fail = 1;
      }
    }
  }
  return fail;
}
/*}}}*/
static void detect_nontrivial_renames(struct fnode *fileinv, struct fnode *localinv)/*{{{*/
{
  /* The tool is only designed to cope with simple renames, where a file
   * currently named A on the remote server now has its contents in a file B on
   * the local machine.  So we need to rename A to B remotely to fix this up.
   * What we can't cope with is file1 named A remotely / B locally and file2
   * named B remotely / C locally, since this requires the renames to be done
   * in a certain order.  Even worse would be a loop - we certainly don't deal
   * with that.  Also, we don't deal with renames where the existing remote
   * name is now a local directory, or where a remote directory exists with a
   * name that's now a file locally. */

  if (detect_local_dir_remote_file(localinv)) exit(1);
  if (detect_local_file_remote_dir(localinv)) exit(1);
  if (detect_dual_rename(localinv)) exit(1);

}
/*}}}*/
static void reconcile(struct fnode *fileinv, struct fnode *localinv)/*{{{*/
{
  /* Work out what's in each tree that's not in the other one. */
  inner_reconcile(fileinv, localinv);
  find_content_matches(fileinv, localinv);
  
#if 0
  /* Don't compute this for everything here. */
  compute_md5sums(localinv);
#endif

  detect_nontrivial_renames(fileinv, localinv);
  
}
/*}}}*/

static int is_unique_file(struct fnode *e)/*{{{*/
{
  assert(!e->is_dir);
  if ((e->x.file.content_peer == NULL) && 
      ((e->path_peer == NULL) ||
       ((!e->path_peer->is_dir) &&
        (e->path_peer->x.file.content_peer != NULL)))) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/
static int is_stale_file(struct fnode *e)/*{{{*/
{
  assert(!e->is_dir);
  if ((e->x.file.content_peer == NULL) &&
      (e->path_peer != NULL) &&
      (!e->path_peer->is_dir) &&
      (!e->path_peer->x.file.content_peer)) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/
static int is_rename_file(struct fnode *e)/*{{{*/
{
  assert(!e->is_dir);
  if (e->x.file.content_peer &&
      (!e->path_peer || (e->path_peer != e->x.file.content_peer))) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/
static void print_unique(struct fnode *x)/*{{{*/
{
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    if (e->is_dir) {
      if (!e->path_peer) {
        printf("D %s\n", e->path);
      }
      print_unique((struct fnode *) &e->x.dir.next);
    } else {
      if (is_unique_file(e)) {
        printf("F %s\n", e->path);
      }
    }
  }
}
/*}}}*/
static void print_stale(struct fnode *x)/*{{{*/
{
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    if (e->is_dir) {
      print_stale((struct fnode *) &e->x.dir.next);
    } else {
      if (is_stale_file(e)) {
        printf("F %s\n", e->path);
      }
    }
  }
}
/*}}}*/
static void print_renames(struct fnode *x)/*{{{*/
{
  struct fnode *e;
  for (e = x->next; e != x; e = e->next) {
    if (e->is_dir) {
      print_renames(dir_children(e));
    } else {
      if (is_rename_file(e)) {
        printf("R %s->%s\n", e->path, e->x.file.content_peer->path);
      }
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
  printf("\n\nRENAMES REQUIRED IN REMOTE FILESYSTEM\n");
  print_renames(fileinv);
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
  struct fnode *a;
  for (a = fileinv->next; a != fileinv; a = a->next) {
    if (a->is_dir) {
      remove_dead_files(ctrl_con, dir_children(a), journal);
    } else {
      if (is_unique_file(a)) {
        remove_file(ctrl_con, a, journal);
      }
    }
  }
}
/*}}}*/
static void remove_old_directories(struct FTP *ctrl_con, struct fnode *fileinv, FILE *journal)/*{{{*/
{
  struct fnode *a, *next_a;
  for (a = fileinv->next; a != fileinv; a = next_a) {
    next_a = a->next;
    if (a->is_dir) {
      remove_old_directories(ctrl_con, dir_children(a), journal);
      if (!a->path_peer) {
        remove_directory(ctrl_con, a, journal);
      }
    }
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
    char *md5buf = format_md5(file);
    fprintf(journal, "F %8d %08lx %s %s\n", file->x.file.size, file->x.file.mtime, md5buf, file->path);
    fflush(journal);
    printf("Done creating new remote file %s (%d bytes)  \n", file->path, file->x.file.size);
    fflush(stdout);
  } else {
    fprintf(stderr, "FAILED TO CREATE FILE %s ON REMOTE SIZE, ABORTING\n", file->path);
    exit(1);
  }
}
/*}}}*/
static void rename_file(struct FTP *ctrl_con, struct fnode *file, FILE *journal)/*{{{*/
{
  int status;
  char *from, *to;
  from = file->x.file.content_peer->path;
  to   = file->path;
  status = ftp_rename(ctrl_con, from, to);
  if (status) {
    char *md5buf;
    fprintf(journal, "Z %s\n", from);
    md5buf = format_md5(file);
    fprintf(journal, "F %8d %08lx %s %s\n", file->x.file.size, file->x.file.mtime, md5buf, to);
    fflush(journal);
    printf("Renamed %s -> %s\n", from, to);
    fflush(stdout);
  } else {
    fprintf(stderr, "FAILED TO RENAME %s -> %s ON REMOTE SIZE, ABORTING\n", from, to);
    exit(1);
  }
}
  /*}}}*/
static void add_new_files(struct FTP *ctrl_con, struct fnode *localinv, FILE *journal)/*{{{*/
{
  struct fnode *a;
  for (a = localinv->next; a != localinv; a = a->next) {
    if (a->is_dir) {
      add_new_files(ctrl_con, dir_children(a), journal);
    } else {
      if (is_unique_file(a)) {
        create_file(ctrl_con, a, journal);
      }
    }
  }
}
/*}}}*/
static void create_new_directories(struct FTP *ctrl_con, struct fnode *localinv, FILE *journal)/*{{{*/
{
  struct fnode *a;
  for (a = localinv->next; a != localinv; a = a->next) {
    if (a->is_dir) {
      if (a->path_peer == NULL) {
        create_directory(ctrl_con, a, journal);
      }
      add_new_files(ctrl_con, dir_children(a), journal);
    }
  }
}
/*}}}*/
static void rename_moved_files(struct FTP *ctrl_con, struct fnode *localinv, FILE *journal)/*{{{*/
{
  struct fnode *a;
  for (a = localinv->next; a != localinv; a = a->next) {
    if (a->is_dir) {
      rename_moved_files(ctrl_con, dir_children(a), journal);
    } else {
      if (is_rename_file(a)) {
        rename_file(ctrl_con, a, journal);
      }
    }
  }
}
/*}}}*/
static void update_file(struct FTP *ctrl_con, struct fnode *file, FILE *journal)/*{{{*/
{
  int status;
  /* file points to the entry in the fileinv inventory */
  struct fnode *local_peer = file->path_peer;
  /* FIXME : magic symlink */
  /* ? do we need to delete the file first for safety (according to STOR in
   * RFC959, no.) */
  printf("Starting to update %s (%d bytes)\r", file->path, local_peer->x.file.size);
  fflush(stdout);
  status = ftp_write(ctrl_con, file->path, file->path);
  /* FIXME : md5sum */
  if (status) {
    char *md5buf = format_md5(local_peer);
    fprintf(journal, "F %8d %08lx %s %s\n", local_peer->x.file.size, local_peer->x.file.mtime, md5buf, file->path);
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
    if (a->is_dir) {
      update_stale_files(ctrl_con, dir_children(a), journal);
    } else {
      if (is_stale_file(a)) {
        update_file(ctrl_con, a, journal);
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
  create_new_directories(ctrl_con, localinv, journal);
  rename_moved_files(ctrl_con, localinv, journal);
  add_new_files(ctrl_con, localinv, journal);
  update_stale_files(ctrl_con, fileinv, journal);
  remove_old_directories(ctrl_con, fileinv, journal);

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
    upload_for_real(ctrl_con, localinv, fileinv, listing_file);
    ftp_close(ctrl_con);
  }

  return 0;
}
/*}}}*/

/* arch-tag: 33897a53-8f50-4ecb-802b-5ddfac6e4a95
*/
