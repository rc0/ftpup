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
#include "memory.h"


static void add_fnode(struct fnode *parent, struct fnode *new_fnode)/*{{{*/
{
  new_fnode->prev = parent->prev;
  new_fnode->next = parent;
  parent->prev->next = new_fnode;
  parent->prev = new_fnode;
}
/*}}}*/

static void scan_one_dir(const char *path, struct fnode *a)/*{{{*/
{
  /* a is the list onto which the new entries are appended. */
  DIR *d;
  struct dirent *de;
  int pathlen;

  pathlen = strlen(path);

  d = opendir(path);
  if (!d) return; /* tough */
  while ((de = readdir(d))) {
    char *full_path;
    struct stat sb;
    int namelen, totallen;
    if (!strcmp(de->d_name, ".")) continue;
    if (!strcmp(de->d_name, "..")) continue;

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
        add_fnode(a, nfn);
      } else if (S_ISDIR(sb.st_mode)) {
        struct fnode *nfn;
        nfn = new(struct fnode);
        nfn->name = new_string(de->d_name);
        nfn->path = new_string(full_path);
        nfn->is_dir = 1;
        nfn->x.dir.next = nfn->x.dir.prev = (struct fnode *) &nfn->x.dir;
        add_fnode(a, nfn);
        scan_one_dir(full_path, (struct fnode *) &nfn->x.dir.next);
      } else {
        fprintf(stderr, "Can't handle %s, type not supported\n", full_path);
      }
    }
    free(full_path);
  }
  closedir(d);
}
/*}}}*/
struct fnode *make_localinv(void)/*{{{*/
{
  struct fnode *result;

  result = new(struct fnode);
  result->next = result->prev = result;
  scan_one_dir(".", result);
  return result;
};
/*}}}*/

void print_inventory(struct fnode *a)/*{{{*/
{
  struct fnode *b;
  for (b = a->next; b != a; b = b->next) {
    if (b->is_dir) {
      printf("D %s %s\n", b->name, b->path);
      print_inventory((struct fnode *) &b->x.dir.next);
    } else {
      printf("F %8d %08lx %s %s\n", b->x.file.size, b->x.file.mtime,
             b->name, b->path);
    }
  }
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

/* arch-tag: 1f24cd61-88e2-481c-b7e5-44a81033d38c
*/

