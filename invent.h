/* Header file for inventory-related stuff. */

#ifndef INVENT_H
#define INVENT_H

#include <sys/types.h>
#include <time.h>

struct fnode {/*{{{*/
  /* Represent a file or a directory. */

  /* Linked list through peers in a particular directory. */
  struct fnode *next;
  struct fnode *prev;

  char *name; /* name within parent directory */
  char *path; /* complete path from top of tree */
  int is_dir;
  int is_unique; /* 1 if only in this tree, 0 if in peer too. */

  union {
    struct {
      size_t size;
      time_t mtime;
      /* Eventually : perms? md5sum? */
    } file;
    struct {
      /* Linked list of entries in the subdirectory. */
      struct fnode *next;
      struct fnode *prev;
    } dir;
  } x;
};
/*}}}*/

/* Assume already in the right directory at the point this is called. */
struct fnode *make_localinv(void);
struct fnode *make_fileinv(const char *listing);
struct fnode *make_remoteinv(const char *hostname, const char *username, const char *password, const char *remote_root);

void print_inventory(struct fnode *a);
#endif /* INVENT_H */

/* arch-tag: c12bf55b-97e2-4ffe-9092-2345b28d57c9
*/

