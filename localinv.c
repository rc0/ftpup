/* Functions to build an inventory of what's on the local system now. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ftp.h"
#include "invent.h"
#include "namecheck.h"
#include "memory.h"

void add_fnode_at_end(struct fnode *parent, struct fnode *new_fnode)/*{{{*/
{
  new_fnode->prev = parent->prev;
  new_fnode->next = parent;
  parent->prev->next = new_fnode;
  parent->prev = new_fnode;
}
/*}}}*/
void add_fnode_at_start(struct fnode *parent, struct fnode *new_fnode)/*{{{*/
{
  new_fnode->prev = parent;
  new_fnode->next = parent->next;
  parent->next->prev = new_fnode;
  parent->next = new_fnode;
}
/*}}}*/

/* FIXME : this stuff needs to be user-configurable eventually. */
static int reject_name(const char *name, struct namecheck *global_nc, struct namecheck *local_nc) {/*{{{*/
  enum nc_result ncr;
  if (local_nc) {
    ncr = lookup_namecheck(local_nc, name);
    if (ncr == NC_PASS)      return 0;
    else if (ncr == NC_FAIL) return 1;
  }
  if (global_nc) {
    ncr = lookup_namecheck(global_nc, name);
    if (ncr == NC_PASS)      return 0;
    else if (ncr == NC_FAIL) return 1;
    else {
      fprintf(stderr, "Filename <%s> not matched by [GLOBAL_]UPLOAD file rules\n", name);
      exit(1);
    }
  } else {
    /* With no control files, default to accepting every file */
    return 0;
  }
}
/*}}}*/
static void scan_one_dir(const char *path, struct namecheck *global_nc, struct fnode *a)/*{{{*/
{
  /* a is the list onto which the new entries are appended. */
  DIR *d;
  struct dirent *de;
  int pathlen;
  struct namecheck *local_nc;

  pathlen = strlen(path);

  /* This returns null if the file isn't there. */
  local_nc = make_namecheck_dir(path, "@@UPLOAD@@");
  d = opendir(path);
  if (!d) return; /* tough */
  while ((de = readdir(d))) {
    char *full_path;
    struct stat sb;
    int namelen, totallen;
    if (!strcmp(de->d_name, ".")) continue;
    if (!strcmp(de->d_name, "..")) continue;
    if (reject_name(de->d_name, global_nc, local_nc)) continue;

    /* FIXME : Need some glob handling here to reject file patterns that the
     * user doesn't want to push. */
    
    if (strcmp(".", path)) {
      namelen = strlen(de->d_name);
      totallen = pathlen + 1 + namelen;
      full_path = new_array(char, totallen + 1);
      strcpy(full_path, path);
      strcat(full_path, "/");
      strcat(full_path, de->d_name);
    } else {
      full_path = new_string(de->d_name);
    }
    if (stat(full_path, &sb) >= 0) {
      if (S_ISREG(sb.st_mode)) {
        struct fnode *nfn;
        nfn = new(struct fnode);
        nfn->name = new_string(de->d_name);
        nfn->path = new_string(full_path);
        nfn->is_dir = 0;
        nfn->x.file.size = sb.st_size;
        nfn->x.file.mtime = sb.st_mtime;
        nfn->x.file.peer = NULL;
        add_fnode_at_end(a, nfn);
      } else if (S_ISDIR(sb.st_mode)) {
        struct fnode *nfn;
        nfn = new(struct fnode);
        nfn->name = new_string(de->d_name);
        nfn->path = new_string(full_path);
        nfn->is_dir = 1;
        nfn->x.dir.next = nfn->x.dir.prev = (struct fnode *) &nfn->x.dir;
        add_fnode_at_start(a, nfn);
        scan_one_dir(full_path, global_nc, (struct fnode *) &nfn->x.dir.next);
      } else {
        fprintf(stderr, "Can't handle %s, type not supported\n", full_path);
      }
    }
    free(full_path);
  }
  closedir(d);
  if (local_nc) free_namecheck(local_nc);
}
/*}}}*/
struct fnode *make_localinv(const char *to_avoid)/*{{{*/
{
  struct fnode *result;
  struct namecheck *global_nc;

  global_nc = make_namecheck("@@GLOBAL_UPLOAD@@");
  result = new(struct fnode);
  result->next = result->prev = result;
  scan_one_dir(".", global_nc, result);
  return result;
};
/*}}}*/

static void inner_print_inventory(struct fnode *a, FILE *out)/*{{{*/
{
  struct fnode *b;

  for (b = a->next; b != a; b = b->next) {
    if (b->is_dir) {
      fprintf(out ? out : stdout, "D                   %s\n", b->path);
      inner_print_inventory((struct fnode *) &b->x.dir.next, out);
    } else {
      fprintf(out ? out : stdout, "F %8d %08lx %s\n", (int)b->x.file.size, b->x.file.mtime, b->path);
    }
  }
}
/*}}}*/

void print_inventory(struct fnode *a, const char *to_file, const char *hostname, const int port_number, const char *username, const char *remote_root)/*{{{*/
{
  FILE *out;
  if (to_file) {
    out = fopen(to_file, "w");
  } else {
    out = NULL;
  }

  fprintf(out, "H %s\n", hostname);
  fprintf(out, "U %s\n", username);
  fprintf(out, "P %d\n", port_number);
  if (remote_root) {
    fprintf(out, "R %s\n", remote_root);
  }

  inner_print_inventory(a, out);
  if (out) fclose(out);
}
/*}}}*/

#ifdef TEST
int main (int argc, char **argv)/*{{{*/
{
  struct fnode *inventory;
  inventory = make_localinv();
  print_inventory(inventory);
  return 0;
}
/*}}}*/
#endif

