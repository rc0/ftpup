/*
 * Generate inventory of remote FTP site
 * */

#include <stdio.h>
#include <sys/types.h>

#include "ftp.h"
#include "invent.h"
#include "memory.h"

static void add_fnode(struct fnode *parent, struct fnode *new_fnode)/*{{{*/
{
  new_fnode->prev = parent->prev;
  new_fnode->next = parent;
  parent->prev->next = new_fnode;
  parent->prev = new_fnode;
}
/*}}}*/

static void scan_one_dir(struct FTP *ctrl_con, const char *path, struct fnode *x)/*{{{*/
{
  struct FTP_stat *files;
  int n_files;
  int i;
  int dirlen, namelen, totallen;
  char *full_path;

  dirlen = strlen(path);

  ftp_lsdir(ctrl_con, path, &files, &n_files);
  for (i=0; i<n_files; i++) {
    if (!strcmp(files[i].name, ".")) continue;
    if (!strcmp(files[i].name, "..")) continue;
    if (strcmp(".", path)) {
      namelen = strlen(files[i].name);
      totallen = dirlen + 1 + namelen;
      full_path = new_array(char, totallen + 1);
      strcpy(full_path, path);
      strcat(full_path, "/");
      strcat(full_path, files[i].name);
    } else {
      full_path = new_string(files[i].name);
    }

    if (files[i].is_dir) {
      struct fnode *nfn;
      nfn = new(struct fnode);
      nfn->name = files[i].name;
      nfn->path = full_path;
      nfn->is_dir = 1;
      nfn->x.dir.next = nfn->x.dir.prev = (struct fnode *) &nfn->x.dir;
      add_fnode(x, nfn);
      scan_one_dir(ctrl_con, full_path, (struct fnode *) &nfn->x.dir.next);
    } else {
      /* regular file */
      struct fnode *nfn;
      nfn = new(struct fnode);
      nfn->name = files[i].name;
      nfn->path = full_path;
      nfn->is_dir = 0;
      nfn->x.file.size = files[i].size;
      nfn->x.file.mtime = 0;
      /* TODO : something about perms */
      add_fnode(x, nfn);
    }
  }
  free(files);
}
/*}}}*/

struct fnode *make_remoteinv(const char *hostname, const char *username, const char *password, const char *remote_root)/*{{{*/
{
  struct FTP *ftp_con;
  struct fnode *result;

  ftp_con = ftp_open(hostname, username, password);
  if (remote_root) {
    ftp_cwd(ftp_con, remote_root);
  }
  result = new(struct fnode);
  result->next = result->prev = result;
  scan_one_dir(ftp_con, ".", result);
  ftp_close(ftp_con);
  return result;
}
/*}}}*/

/* arch-tag: afdeef23-00ab-483e-b0fc-08973895f87a
*/

