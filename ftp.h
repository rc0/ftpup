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
                            const char *password);

extern int ftp_close(struct FTP *);

extern int ftp_cwd(struct FTP *, const char *new_root_dir);

extern int ftp_write(struct FTP *,
                     const char *filename, /* local path */
                     const char *remote_path); /* path on remote server */

extern int ftp_read(struct FTP *,
                    const char *remote_path,
                    const char *filename); /* local path to write data to */

extern int ftp_rename(struct FTP *,
                      const char *old_path, /* old remote path */
                      const char *new_path); /* new remote path */

extern int ftp_delete(struct FTP *,
                      const char *remote_path);

extern int ftp_mkdir(struct FTP *,
                     const char *remote_path);

extern int ftp_rmdir(struct FTP *,
                     const char *remote_path);

extern int ftp_stat(struct FTP *,
                    const char *remote_path,
                    struct FTP_stat *);

extern int ftp_lsdir(struct FTP *,
                     const char *remote_dir_path,
                     struct FTP_stat **file_data,
                     int *nfiles);

/* arch-tag: 522f1f64-1ee6-4e68-be91-ffaab000be9c
*/

