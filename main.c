
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "invent.h"
#include "memory.h"

int verbose = 0;

static void usage(void)
{
  /* TODO : write this. */
}

int main (int argc, char **argv) {

  struct fnode *reminv;
  char *hostname = NULL;
  char *username = NULL;
  char *password = NULL;
  char *remote_root = NULL;
  char *listing_file = NULL;

  /* Download the remote tree to create an initial inventory listing. */
  int do_remote_inv = 0;
  
  /* Download the remote tree and see what is out of step with the listing file. */
  int do_lint = 0;
  
  /* Actually do the upload operation. */
  int do_upload = 0;
  
  /* Work out what would get uploaded/removed and show to user */
  int do_dummy_upload = 0;
  
  while (++argv, --argc) {
    if ((*argv)[0] == '-') {
      if (!strcmp(*argv, "-u")) {
        --argc, ++argv;
        username = *argv;
      } else if (!strcmp(*argv, "-p")) {
        --argc, ++argv;
        password = *argv;
      } else if (!strcmp(*argv, "-r")) {
        --argc, ++argv;
        remote_root = *argv;
      } else if (!strcmp(*argv, "-l")) {
        --argc, ++argv;
        listing_file = *argv;
      } else if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
        usage();
        exit(0);
      } else if (!strcmp(*argv, "-U") || !strcmp(*argv, "--upload")) {
        do_upload = 1;
      } else if (!strcmp(*argv, "-N") || !strcmp(*argv, "--dummy")) {
        do_dummy_upload = 1;
      } else if (!strcmp(*argv, "-R") || !strcmp(*argv, "--remote-inventory")) {
        do_remote_inv = 1;
      } else if (!strcmp(*argv, "-v") || !strcmp(*argv, "--verbose")) {
        verbose = 1;
      } else {
        fprintf(stderr, "Unrecognized option %s\n", *argv);
        exit(2);
      }
    } else {
      hostname = *argv;
    }
  }

  if (!do_remote_inv && !do_lint && !do_upload && !do_dummy_upload) {
    fprintf(stderr, "One of the options -R, -L, -U or -N is required\n");
    exit(1);
  }

  if (do_remote_inv || do_upload) {
    if (!password) {
      password = getpass("PASSWORD: ");
      password = new_string(password);
    }
  }

  if (!listing_file) {
    listing_file = "@@LISTING@@";
  }
  
  if (do_remote_inv) {
    if (!hostname) {
      fprintf(stderr, "-R requires hostname\n");
      exit(1);
    }
    if (!username) {
      fprintf(stderr, "-R requires username\n");
      exit(1);
    }
    reminv = make_remoteinv(hostname, username, password, remote_root);
    print_inventory(reminv, listing_file, hostname, username, remote_root);
  } else if (do_lint) {
  } else if (do_upload) {
    upload(password, 0, listing_file);
  } else if (do_dummy_upload) {
    upload(password, 1, listing_file);
  }

  return 0;
}

/* arch-tag: 14fd4ccf-b03d-44bf-bdf3-8d258543a1ae
*/


