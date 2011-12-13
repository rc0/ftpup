#include <crypt.h>
int main (int argc, char **argv) {
  if (argc > 1) {
    printf("%s\n", crypt(argv[1], "Aa"));
    return 0;
  } else {
    return 1;
  }
}


