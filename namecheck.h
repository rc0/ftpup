#ifndef NAMECHECK_H
#define NAMECHECK_H

enum nc_result {
  NC_FAIL,
  NC_PASS,
  NC_UNMATCHED
};

struct namecheck;

extern struct namecheck *make_namecheck_dir(const char *dir, const char *filename);
extern struct namecheck *make_namecheck(char *filename);
extern void free_namecheck(struct namecheck *);

extern enum nc_result lookup_namecheck(const struct namecheck *, const char *filename);

#endif /* NAMECHECK_H */

/* arch-tag: 5545aedb-7a46-4b36-8923-8838f34a922b
 * */
