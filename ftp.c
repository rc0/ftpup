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

extern int verbose;

/* Private definition of opaque structure. */
struct FTP {/*{{{*/
  int fd;        /* fd of the control socket */
  int active;    /* 1 if use active on the data connection, 0 for passive */
  int listen_fd; /* listening fd for active mode */
  
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
        if (verbose) {
          printf("Got line [%s]\n", result);
        }
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
struct FTP *ftp_open(const char *hostname, const char *username, const char *password, int active_ftp)/*{{{*/
{
  struct FTP *result;
  struct hostent *host;
  struct sockaddr_in addr;
  unsigned char *address0;
  unsigned long ip;
  int addrlen;
  int status;

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
  status = read_status(result);
  if (verbose) {
    printf("Got %d from hello string\n", status);
  }

  put_cmd(result, "USER", username);
  status = read_status(result);
  if (verbose) {
    printf("Got %d from USER comamnd\n", status);
  }
  put_cmd(result, "PASS", password);
  status = read_status(result);
  if (verbose) {
    printf("Got %d from PASS comamnd\n", status);
  }
  if (status >= 500) {
    fprintf(stderr, "Password authentication failed\n");
    exit(1);
  }

  result->active = active_ftp;

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

int open_passive_data_con(struct FTP *ctrl_con)/*{{{*/
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
  if (verbose) {
    printf("Got status %d from PASV command\n", status);
  }
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
    if (verbose) {
      printf("Host IP=%08lx port=%d\n", host_ip, port);
    }

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

static void setup_active_data_con(struct FTP *ctrl_con)/*{{{*/
{
  struct sockaddr_in ctrl_sock_addr, data_sock_addr;
  int ctrl_sock_addr_len, data_sock_addr_len;
  int status;
  unsigned long ip_addr;
  unsigned short port;
  int listen_fd, accept_fd;
  char port_string[24];

  ctrl_sock_addr_len = sizeof(struct sockaddr_in);
  status = getsockname(ctrl_con->fd, (struct sockaddr *) &ctrl_sock_addr, &ctrl_sock_addr_len);
  if (status < 0) {
    perror("getsocknamd(ctrl_con)");
    exit(1);
  }
  ip_addr = ntohl(ctrl_sock_addr.sin_addr.s_addr);
  port = ntohs(ctrl_sock_addr.sin_port);

  data_sock_addr.sin_family = AF_INET;
  data_sock_addr.sin_addr.s_addr = ctrl_sock_addr.sin_addr.s_addr;
  data_sock_addr.sin_port = 0;
  data_sock_addr_len = sizeof(data_sock_addr);

  listen_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket(listen_fd)");
    exit(1);
  }

  if (bind(listen_fd, (struct sockaddr *) &data_sock_addr, data_sock_addr_len) < 0) {
    perror("bind(listen_fd)");
    exit(1);
  }

  if (listen(listen_fd, 1) < 0) {
    perror("listen(listen_fd)");
    exit(1);
  }

  /* Get actual port number */
  status = getsockname(listen_fd, (struct sockaddr *) &data_sock_addr, &data_sock_addr_len);
  if (status < 0) {
    perror("getsockname(listen_fd)");
    exit(1);
  }

  ip_addr = htonl(data_sock_addr.sin_addr.s_addr);
  port = ntohs(data_sock_addr.sin_port);
  sprintf(port_string, "%d,%d,%d,%d,%d,%d",
          (int)((ip_addr>>24) & 0xff),
          (int)((ip_addr>>16) & 0xff),
          (int)((ip_addr>> 8) & 0xff),
          (int)((ip_addr    ) & 0xff),
          (int)((port   >> 8) & 0xff),
          (int)((port       ) & 0xff));
  put_cmd(ctrl_con, "PORT", port_string);
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from PORT %s command\n", status, port_string);
  }
  if (status >= 400) {
    fprintf(stderr, "Failed to set port number\n");
    exit(1);
  }

  ctrl_con->listen_fd = listen_fd;
  return;
}
/*}}}*/

static int open_active_data_con(struct FTP *ctrl_con)/*{{{*/
{
  struct sockaddr_in peer_sock_addr;
  int peer_sock_addr_len;
  int status;
  unsigned long ip_addr;
  unsigned short port;
  int accept_fd;

  peer_sock_addr_len = sizeof(peer_sock_addr);
  accept_fd = accept(ctrl_con->listen_fd, (struct sockaddr *) &peer_sock_addr, &peer_sock_addr_len);
  ip_addr = htonl(peer_sock_addr.sin_addr.s_addr);
  port = ntohs(peer_sock_addr.sin_port);

  /* Don't need to listen any longer. */
  close(ctrl_con->listen_fd);

  return accept_fd;

}
/*}}}*/

static void borked_delete(struct FTP *ctrl_con)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "DELE", "loads_a_crap");
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from DELE comamnd\n", status);
  }
}
/*}}}*/
static void borked_rename(struct FTP *ctrl_con)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "RNFR", "loads_a_crap");
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from RNFR comamnd\n", status);
  }
  if (status >= 500) return;
  put_cmd(ctrl_con, "RNTO", "loads_a_money");
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from RNTO comamnd\n", status);
  }

}
/*}}}*/
int ftp_cwd(struct FTP *con, const char *new_root_dir)/*{{{*/
{
  int status;
  put_cmd(con, "CWD", new_root_dir);
  status = read_status(con);
  if (verbose) {
    printf("Got %d from CWD comamnd\n", status);
  }
  return 0;
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
static void append_file(struct file_list *fl, const char *name, int size, int perms, int is_dir)/*{{{*/
{
  struct file_list *new_fl;

  new_fl = new(struct file_list);
  new_fl->name = new_string(name);
  new_fl->size = size;
  new_fl->perms = perms;
  new_fl->is_dir = is_dir;
  
  new_fl->next = fl;
  new_fl->prev = fl->prev;
  fl->prev->next = new_fl;
  fl->prev = new_fl;
  return;
}
/*}}}*/

int ftp_lsdir(struct FTP *ctrl_con, const char *dir_path,/*{{{*/
              struct FTP_stat **file_data,
              int *n_files)
{
  int data_fd;
  char line[1024];
  char *fields[16];
  int n_fields;
  struct file_list fl, *a, *next_a;
  int N, i;
  int status;
  FILE *in;

  fl.next = fl.prev = &fl;
  
  if (ctrl_con->active) {
    setup_active_data_con(ctrl_con);
  } else {
    data_fd = open_passive_data_con(ctrl_con);
  }

  if (!strcmp(dir_path, ".")) {
    /* Otherwise SuperH's FTP server, for one, gets confused */
    put_cmd(ctrl_con, "LIST -a", NULL);
  } else {
    put_cmd(ctrl_con, "LIST -a", dir_path);
  }

  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got status %d after LIST %s\n", status, dir_path);
  }

  if (ctrl_con->active) {
    data_fd = open_active_data_con(ctrl_con);
  }

  N = 0;
  in = fdopen(data_fd, "rb");
  while (fgets(line, sizeof(line), in)) {
    int size;
    int perms, is_dir;
    char *name;
    strip_termination(line);
    /* Some servers (e.g. SuperH's one) report a total line at the start of the
     * listing. */
    if (!strncmp(line, "total", 5)) continue;
    split_into_fields(line, fields, &n_fields);
    if (n_fields < 8) {
      fprintf(stderr, "Didn't see expected number of fields\n");
      exit(1);
    }
    name = fields[n_fields - 1];
    size = atoi(fields[n_fields - 5]);
    parse_perms(fields[0], &perms, &is_dir);
    if (strcmp(name, ".") && strcmp(name, "..")) {
      /* Don't add . or .. entries to the list else it will recurse infinitely.
       * */
      ++N;
      append_file(&fl, name, size, perms, is_dir);
    }
  }

  fclose(in);
  /* might need more actions to close an active connection? */
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got status %d after LIST %s data transfer\n", status, dir_path);
  }
  
  /* Map to array */
  *n_files = N;
  *file_data = new_array(struct FTP_stat, N);
  for (i=0, a = fl.next; i < N; i++, a = next_a) {
    (*file_data)[i].is_dir = a->is_dir;
    (*file_data)[i].size = a->size;
    (*file_data)[i].perms = a->perms;
    (*file_data)[i].name = a->name;
    next_a = a->next;
    free(a);
  }
}
/*}}}*/

static int status_map(int status) {/*{{{*/
  if ((status >= 200) && (status < 300)) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/
int ftp_delete(struct FTP *ctrl_con, const char *path)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "DELE", path);
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from DELE comamnd\n", status);
  }
  return status_map(status);
}
/*}}}*/
int ftp_rename(struct FTP *ctrl_con, const char *from_path, const char *to_path)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "RNFR", from_path);
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from RNFR command\n", status);
  }
  if (status >= 500) return;
  put_cmd(ctrl_con, "RNTO", from_path);
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got %d from RNTO command\n", status);
  }
  return status_map(status);
}
/*}}}*/
int ftp_rmdir(struct FTP *ctrl_con, const char *dir_path)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "RMD", dir_path);
  status = read_status(ctrl_con);
  return status_map(status);
}
/*}}}*/
int ftp_mkdir(struct FTP *ctrl_con, const char *dir_path)/*{{{*/
{
  int status;
  put_cmd(ctrl_con, "MKD", dir_path);
  status = read_status(ctrl_con);
  return status_map(status);
}
/*}}}*/
int ftp_write(struct FTP *ctrl_con, const char *local_path, const char *remote_path)/*{{{*/
{
  int data_fd;
  FILE *local, *remote;
  int status, mapped_status;
  char buffer[4096];
  int n;

  local = fopen(local_path, "rb");
  if (!local) {
    fprintf(stderr, "Could not open local file %s\n", local_path);
    return 0;
  }

  if (ctrl_con->active) {
    setup_active_data_con(ctrl_con);
  } else {
    data_fd = open_passive_data_con(ctrl_con);
  }
  put_cmd(ctrl_con, "STOR", remote_path);
  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got status %d after STOR %s->%s\n", status, local_path, remote_path);
  }
  if (status >= 400) return 0;

  if (ctrl_con->active) {
    data_fd = open_active_data_con(ctrl_con);
  }

  remote = fdopen(data_fd, "w");
  
  while (1) {
    n = fread(buffer, 1, 4096, local);
    if (!n) break;
    fwrite(buffer, 1, n, remote);
  }

  fclose(remote);
  fclose(local);

  status = read_status(ctrl_con);
  if (verbose) {
    printf("Got status %d after STOR %s->%s\n", status, local_path, remote_path);
  }

  return status_map(status);
}
/*}}}*/

/* arch-tag: 4b5f74ca-8738-4367-ad53-12185fb7bd85
*/

