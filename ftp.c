/*
 * Header comments */

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include "ftp.h"
#include "memory.h"

/* Private definition of opaque structure. */
struct FTP {/*{{{*/
  int fd;   /* fd of the socket */
  
  char readbuf[4096];
  char *bufptr;
};
/*}}}*/
static char *read_line(struct FTP *con)/*{{{*/
{
  char *result;
  int n;
  char *p;
  
  while (1) {
    n = read(con->fd, con->bufptr, con->readbuf + sizeof(con->readbuf) - con->bufptr);
    if (n < 0) {
      perror("read");
      exit(1);
    }
    if (n == 0) {
      return NULL;
    }
    con->bufptr += n;
    p = con->readbuf;
    while (p < con->bufptr - 1) {
      if (p[0]=='\r' && p[1] == '\n') {
        /* match */
        int len = p - con->readbuf;
        int remain;
        result = new_array(char, len+1);
        memcpy(result, con->readbuf, len);
        result[len] = 0;
        printf("Got line [%s]\n", result);
        remain = con->bufptr - (p+2);
        if (remain > 0) {
          memmove(con->readbuf, p+2, remain);
          con->bufptr -= (len + 2);
        } else {
          con->bufptr = con->readbuf;
        }
        return result;
      } else {
        p++;
      }
    }
  }
}
/*}}}*/
static int get_status(const char *x)/*{{{*/
{
  int val;
  if (isdigit(x[0]) && isdigit(x[1]) && isdigit(x[2]) && (x[3] == ' ')) {
    val = 100*(x[0] - '0') + 10*(x[1] - '0') + (x[2] - '0');
  } else {
    val = 0;
  }
  return val;
}
/*}}}*/
static int read_status(struct FTP *con)/*{{{*/
{
  char *line;
  int status;
  do {
    line = read_line(con);
    status = get_status(line);
    free(line);
  } while (status == 0);
  return status;
}
/*}}}*/
static void put_cmd(struct FTP *con, const char *cmd, const char *arg)/*{{{*/
{
  char *xcmd;
  int len;
  if (arg) {
    len = strlen(cmd) + strlen(arg) + 3;
    xcmd = new_array(char, len + 1);
    sprintf(xcmd, "%s %s\r\n", cmd, arg);
  } else {
    len = strlen(cmd) + 2;
    xcmd = new_array(char, len + 1);
    sprintf(xcmd, "%s\r\n", cmd);
  }
  /* FIXME : Ought to check for errors. */
  write(con->fd, xcmd, len);
  free(xcmd);
}
/*}}}*/
struct FTP *ftp_open(const char *hostname, const char *username, const char *password)/*{{{*/
{
  struct FTP *result;
  struct hostent *host;
  struct sockaddr_in addr;
  unsigned char *address0;
  unsigned long ip;
  int addrlen;

  result = new(struct FTP);
  result->bufptr = result->readbuf;
  
  host = gethostbyname(hostname);
  if (!host) return NULL;
  address0 = host->h_addr_list[0];
  ip = ((((unsigned long) address0[0]) << 24) |
        (((unsigned long) address0[1]) << 16) |
        (((unsigned long) address0[2]) <<  8) |
        (((unsigned long) address0[3])));

  addr.sin_family = AF_INET;
  addr.sin_port   = htons(21); /* FTP */
  addr.sin_addr.s_addr = htonl(ip);
  addrlen = sizeof(addr);

  result->fd = socket(PF_INET, SOCK_STREAM, 0);
  if (result->fd < 0) {
    perror("socket");
    return NULL;
  }

  if (connect(result->fd, (struct sockaddr *) &addr, addrlen) < 0) {
    perror("connect");
    return NULL;
  }

  /* Read hello string */
  printf("Got %d from hello string\n", read_status(result));

  put_cmd(result, "USER", username);
  printf("Got %d from USER comamnd\n", read_status(result));
  put_cmd(result, "PASS", password);
  printf("Got %d from PASS comamnd\n", read_status(result));

  return result;

}
/*}}}*/
int ftp_close(struct FTP *con)/*{{{*/
{
  close(con->fd);
  free(con);
  return 0;
}
/*}}}*/

static int open_data_con(struct FTP *ctrl_con)/*{{{*/
{
  int status;
  char *line;
  char *p;
  unsigned int h0, h1, h2, h3, p0, p1;
  unsigned long host_ip;
  unsigned short port;
  int data_fd;
  struct sockaddr_in data_addr;
  int addrlen;
    
  put_cmd(ctrl_con, "PASV", NULL);
  line = read_line(ctrl_con);
  status = get_status(line);
  printf("Got status %d from PASV command\n", status);
  if (status != 227) {
    fprintf(stderr, "Could not configure passive\n");
    exit(1);
  }

  /* parse host and port */
  for (p=line; *p; p++) {
    if (*p == '(') break;
  }
  if (*p) {
    sscanf(p+1, "%u,%u,%u,%u,%u,%u", &h0, &h1, &h2, &h3, &p0, &p1);
    host_ip = ((h0 & 0xff) << 24) | ((h1 & 0xff) << 16) | ((h2 & 0xff) << 8) | (h3 & 0xff);
    port = ((p0 & 0xff) << 8) | (p1 & 0xff);
    printf("Host IP=%08lx port=%d\n", host_ip, port);

    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port);
    data_addr.sin_addr.s_addr = htonl(host_ip);
    addrlen = sizeof(data_addr);

    data_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (data_fd < 0) {
      perror("socket(data_fd)");
      exit(1);
    }
    if (connect(data_fd, (struct sockaddr *) &data_addr, addrlen) < 0) {
      perror("connect(data_fd)");
      exit(1);
    }
    return data_fd;

  } else {
    fprintf(stderr, "Could not read host and port\n");
    exit(1);
  }
}
/*}}}*/

static void ftp_list(struct FTP *ctrl_con)/*{{{*/
{
  int data_fd;
  int status;
  FILE *in;
  char line[1024];
  data_fd = open_data_con(ctrl_con);
  put_cmd(ctrl_con, "LIST", NULL);
  status = read_status(ctrl_con);
  in = fdopen(data_fd, "r");
  while (fgets(line, sizeof(line), in)) {
    char *p;
    for (p=line; *p; p++) ;
    for (p--; isspace(*p); p--) *p='\0';
    printf("DATA: %s\n", line);
  }
  fclose(in);
  status = read_status(ctrl_con);
}
/*}}}*/
static void borked_delete(struct FTP *ctrl_con)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "DELE", "loads_a_crap");
  status = read_status(ctrl_con);
  printf("Got %d from DELE comamnd\n", status);
}
/*}}}*/
static void borked_rename(struct FTP *ctrl_con)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "RNFR", "loads_a_crap");
  status = read_status(ctrl_con);
  printf("Got %d from RNFR comamnd\n", status);
  if (status >= 500) return;
  put_cmd(ctrl_con, "RNTO", "loads_a_money");
  status = read_status(ctrl_con);
  printf("Got %d from RNTO comamnd\n", status);

}
/*}}}*/
static void change_directory(struct FTP *con, const char *new_root_dir)/*{{{*/
{
  int status;
  put_cmd(con, "CWD", new_root_dir);
  status = read_status(con);
  printf("Got %d from CWD comamnd\n", status);
}
/*}}}*/
static void usage(void)/*{{{*/
{
  /* FIXME : display usage info. */
}
/*}}}*/

struct file_list {
  struct file_list *next;
  struct file_list *prev;
  char *name;
  int size;
  int perms;
  int is_dir;
  int dir_done;
};

static void strip_termination(char *line)/*{{{*/
{
  char *p;
  for (p=line; *p; p++) ;
  for (p--; isspace(*p); p--) *p='\0';
}
/*}}}*/
static void split_into_fields(char *line, char **fields, int *n_fields)/*{{{*/
{
  char *p = line;
  int i;
  *n_fields = 0;
  for (;;) {
    while (*p && isspace(*p)) {
      p++;
    }
    if (!*p) return;
    fields[*n_fields] = p;
    ++*n_fields;
    p++;
    while (*p && !isspace(*p)) {
      p++;
    }
    if (!*p) return;
    /* Null terminate the field */
    *p++ = '\0';
  }
}
/*}}}*/
static void parse_perms(const char *field, int *perms, int *is_dir)/*{{{*/
{
  *is_dir =  (field[0] == 'd') ? 1 : 0;
  *perms  = ((field[1] == 'r') ? (1<<8) : 0) |
            ((field[2] == 'w') ? (1<<7) : 0) |
            ((field[3] == 'x') ? (1<<6) : 0) |
            ((field[4] == 'r') ? (1<<5) : 0) |
            ((field[5] == 'w') ? (1<<4) : 0) |
            ((field[6] == 'x') ? (1<<3) : 0) |
            ((field[7] == 'r') ? (1<<2) : 0) |
            ((field[8] == 'w') ? (1<<1) : 0) |
            ((field[9] == 'x') ? (1<<0) : 0);
}
/*}}}*/
static void append_file(struct file_list *fl, const char *dir, const char *name, int size, int perms, int is_dir)/*{{{*/
{
  int len;
  struct file_list *new_fl;

  new_fl = new(struct file_list);
  len = strlen(dir) + strlen(name) + 1;
  new_fl->name = new_array(char, len + 1);
  strcpy(new_fl->name, dir);
  strcat(new_fl->name, "/");
  strcat(new_fl->name, name);
  new_fl->size = size;
  new_fl->perms = perms;
  new_fl->is_dir = is_dir;
  new_fl->dir_done = 0;
  
  new_fl->next = fl;
  new_fl->prev = fl->prev;
  fl->prev->next = new_fl;
  fl->prev = new_fl;
  return;
}
/*}}}*/

static void add_to_inventory(struct FTP *ctrl_con, struct file_list *fl, const char *dir)/*{{{*/
{
  int status, data_fd;
  FILE *in;
  char line[1024];
  char *fields[16];
  int n_fields;

  printf("Doing add_to_inventory for %s\n", dir);
  put_cmd(ctrl_con, "PWD", NULL);
  status = read_status(ctrl_con);

  data_fd = open_data_con(ctrl_con);
  if (!strcmp(dir, ".")) {
    /* Otherwise SuperH's FTP server, for one, gets confused */
    put_cmd(ctrl_con, "LIST -a", NULL);
  } else {
    put_cmd(ctrl_con, "LIST -a", dir);
  }

  status = read_status(ctrl_con);
  printf("Got status %d after LIST %s\n", status, dir);
  in = fdopen(data_fd, "rb");
  while (fgets(line, sizeof(line), in)) {
    int size;
    int perms, is_dir;
    char *name;
    strip_termination(line);
    printf("ADD_TO_I : %s\n", line);
    /* Some servers (e.g. SuperH's one) report a total line at the start of the
     * listing. */
    if (!strncmp(line, "total", 5)) continue;
    split_into_fields(line, fields, &n_fields);
    if (n_fields < 8) {
      fprintf(stderr, "Didn't see expected number of fields\n");
      exit(1);
    }
    /* Number of reported fields may vary, e.g. SuperH's server doesn't report
     * both username and group, there's only one column there whose meaning I'm
     * not sure of. */
    name = fields[n_fields - 1];
    size = atoi(fields[n_fields - 5]);
    parse_perms(fields[0], &perms, &is_dir);
    if (strcmp(name, ".") && strcmp(name, "..")) {
      /* Don't add . or .. entries to the list else it will recurse infinitely.
       * */
      append_file(fl, dir, name, size, perms, is_dir);
    }
  }
  fclose(in);
  status = read_status(ctrl_con);
  printf("Got status %d after LIST %s data transfer\n", status, dir);

}
/*}}}*/
static struct file_list *make_inventory(struct FTP *ctrl_con)/*{{{*/
{
  /* List out what is already at the remote site.  Assume already CD'd to the
   * 'root' on the remote site (e.g. /htdocs for force9) */

  struct file_list *result = new(struct file_list);
  struct file_list *a;

  result->next = result->prev = result;
  result->name = NULL;
  result->size = result->perms = result->is_dir = 0;

  add_to_inventory(ctrl_con, result, ".");
  for (a = result->next; a != result; a = a->next) {
    if (a->is_dir && !a->dir_done) {
      add_to_inventory(ctrl_con, result, a->name);
      a->dir_done = 1;
    }
  }

  /* print out; */
  for (a = result->next; a != result; a = a->next) {
    printf("%c %8d %03o %s\n",
           a->is_dir ? 'D' : 'F',
           a->size,
           a->perms,
           a->name);
  }
  
  return result;
}
/*}}}*/

#ifdef TEST
int main (int argc, char **argv) {
  struct FTP *foo;
  int data_fd;
  char *hostname;
  char *username;
  char *password;
  char *remote_root;

  hostname = NULL;
  username = NULL;
  password = NULL;
  remote_root = NULL;

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
      } else if (!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
        usage();
        exit(0);
      } else {
        fprintf(stderr, "Unrecognized option %s\n", *argv);
        exit(2);
      }
    } else {
      hostname = *argv;
    }
  }

  if (!hostname) {
    fprintf(stderr, "ERROR: No hostname specified\n");
    exit(2);
  }

  if (!username) {
    fprintf(stderr, "ERROR: No username specified\n");
    exit(2);
  }

  if (!remote_root) {
    fprintf(stderr, "WARNING: No remote root specified\n");
  }

  if (!password) {
    password = getpass("PASSWORD: ");
    password = new_string(password);
  }
  
  foo = ftp_open(hostname, username, password);
  if (remote_root) {
    change_directory(foo, remote_root);
  }
#if 0
  borked_rename(foo);
  borked_delete(foo);
#endif
#if 0
  ftp_list(foo);
#endif
#if 1
  make_inventory(foo);
#endif
  return 0;
}
#endif

/* arch-tag: 4b5f74ca-8738-4367-ad53-12185fb7bd85
*/

