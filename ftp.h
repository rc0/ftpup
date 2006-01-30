/*
 * FTP client functions
 * */

struct FTP;

struct FTP_stat {
  int is_dir;
  int perms;
  size_t size;
  char *name;
};

extern struct FTP *ftp_open(const char *host,
                            const char *username,
                            const char *password,
                            int active_ftp);

extern int ftp_close(struct FTP *);

extern int ftp_cwd(struct FTP *, const char *new_root_dir);

extern int ftp_write(struct FTP *,
                     const char *local_path, /* local path */
                     const char *remote_path, /* path on remote server */
                     void (*callback)(void*,int),
                     void *cb_arg);

extern int ftp_read(struct FTP *,
                    const char *remote_path,
                    const char *filename); /* local path to write data to */

extern int ftp_rename(struct FTP *,
                      const char *old_path, /* old remote path */
                      const char *new_path); /* new remote path */

/* Return 1 for success, 0 for failure */
extern int ftp_delete(struct FTP *,
                      const char *remote_path);
extern int ftp_rmdir(struct FTP *,
                     const char *remote_path);

extern int ftp_mkdir(struct FTP *,
                     const char *remote_path);

extern int ftp_stat(struct FTP *,
                    const char *remote_path,
                    struct FTP_stat *);

extern int ftp_lsdir(struct FTP *,
                     const char *remote_dir_path,
                     struct FTP_stat **file_data,
                     int *nfiles);

extern int ftp_names(struct FTP *ctrl_con, const char *dir_path,
              char ***names, int *n_names);

extern int ftp_binary(struct FTP *ctrl_con);


/* arch-tag: 522f1f64-1ee6-4e68-be91-ffaab000be9c
*/

