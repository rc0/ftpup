/*
 * Generate inventory of remote FTP site
 * */

#include <stdio.h>
#include <sys/types.h>

#include "ftp.h"
#include "invent.h"
#include "memory.h"

static void scan_one_dir(struct FTP *ctrl_con, const char *path, struct fnode *x)/*{{{*/
{
  struct FTP_stat *files;
  int n_files;
  int i;
  int dirlen, namelen, totallen;
  char *full_path;

  dirlen = strlen(path);

  printf("Scanning directory %s\n", path);
  fflush(stdout);
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
      nfn = make_dir_node(files[i].name, full_path);
      add_fnode_at_start(x, nfn);
      scan_one_dir(ctrl_con, full_path, (struct fnode *) &nfn->x.dir.next);
    } else {
      /* regular file */
      struct fnode *nfn;
      nfn = make_file_node(files[i].name, full_path, files[i].size, 0);
      nfn->x.file.md5_defined = 0;
      add_fnode_at_end(x, nfn);

      /* If file is not writable, update the perms */
#if 0
      if ((files[i].perms & 0200) == 0) {
        ftp_chmod(ctrl_con, full_path, 0644);
      }
#endif
    }
  }
  free(files);
}
/*}}}*/

struct fnode *make_remoteinv(const char *hostname, const char *username, const char *password, const char *remote_root, int active_ftp)/*{{{*/
{
  struct FTP *ftp_con;
  struct fnode *result;

  ftp_con = ftp_open(hostname, username, password, active_ftp);
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

