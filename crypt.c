#include <crypt.h>
int main (int argc, char **argv) {
  if (argc > 1) {
    printf("%s\n", crypt(argv[1], "Aa"));
    return 0;
  } else {
    return 1;
  }
}

/* arch-tag: 8561c3cc-073f-48f2-87e3-d1e064137329
*/

