
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "invent.h"
#include "memory.h"

int verbose = 0;

static void usage(void)
{
  fprintf(stderr, "First time usage:\n"
      "  ftpup -R -u <username> [-P <port_number>] [-r <remote_root>] <hostname>\n"
      "Subsequent use:\n"
      "  ftpup -U        <- do upload\n"
      "  ftpup -U [-a]   <- do upload using active FTP\n"
      "  ftpup -N        <- dry_run : see what would be uploaded\n"
      "Special options:\n"
      "  -l <listing_file> : file containing the remote inventory (default: @@LISTING@@)\n"
      "  -p <password>     : supply FTP password                  (default: prompt for it)\n"
      );
}

int main (int argc, char **argv) {

  struct fnode *reminv;
  char *hostname = NULL;
  int port_number = 21;
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

  int active_ftp = 0;

  while (++argv, --argc) {
    if ((*argv)[0] == '-') {
      if (!strcmp(*argv, "-u")) {
        --argc, ++argv;
        username = *argv;
      } else if (!strcmp(*argv, "-p")) {
        --argc, ++argv;
        password = *argv;
      } else if (!strcmp(*argv, "-P")) {
        --argc, ++argv;
        port_number = atoi(*argv);
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
      } else if (!strcmp(*argv, "-a") || !strcmp(*argv, "--active-ftp")) {
        active_ftp = 1;
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
    reminv = make_remoteinv(hostname, port_number, username, password, remote_root, active_ftp);
    print_inventory(reminv, listing_file, hostname, port_number, username, remote_root);
  } else if (do_lint) {
  } else if (do_upload) {
    upload(password, 0, listing_file, active_ftp);
  } else if (do_dummy_upload) {
    upload(password, 1, listing_file, active_ftp);
  }

  return 0;
}

