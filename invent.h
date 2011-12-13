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
      struct fnode *peer; /* Peer in other tree, if any */
      int is_stale;  /* 1 if different between trees, 0 if the same (don't care if
                        is_unique==1) */
    } file;
    struct {
      /* Linked list of entries in the subdirectory. */
      struct fnode *next;
      struct fnode *prev;
    } dir;
  } x;
};
/*}}}*/

struct remote_params {
  char *hostname;
  int  port_number;
  char *username;
  char *remote_root;
};

void add_fnode_at_start(struct fnode *parent, struct fnode *new_fnode);
void add_fnode_at_end(struct fnode *parent, struct fnode *new_fnode);

/* Assume already in the right directory at the point this is called. */
struct fnode *make_localinv(const char *to_avoid);
struct fnode *make_fileinv(const char *listing, struct remote_params *);
struct fnode *make_remoteinv(const char *hostname, const int port_number, const char *username, const char *password, const char *remote_root, int active_ftp);

void print_inventory(struct fnode *a, const char *to_file, const char *hostname, const int port_number, const char *username, const char *remote_root);

void init_remote_params(struct remote_params *rp);  
int upload(const char *password, int is_dummy_run, const char *listing_file, int active_ftp);

#endif /* INVENT_H */

/* arch-tag: c12bf55b-97e2-4ffe-9092-2345b28d57c9
*/

