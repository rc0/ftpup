
#ifndef MEMORY_H
#define MEMORY_H

#include <stdlib.h>
#include <string.h>

/*{{{  Memory macros*/
#define new_string(s) strcpy((char *) malloc(1+strlen(s)), (s))
#define extend_string(x,s) (strcat(realloc(x, (strlen(x)+strlen(s)+1)), s))
#define new(T) (T *) malloc(sizeof(T))
#define new_array(T, n) (T *) malloc(sizeof(T) * (n))
#define grow_array(T, n, oldX) (T *) ((oldX) ? realloc(oldX, (sizeof(T) * (n))) : malloc(sizeof(T) * (n)))
/*}}}*/

#endif /* MEMMAC_H */

