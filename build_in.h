#ifndef NSPT_BUILD_IN
#define NSPT_BUILD_IN

#include <stddef.h>

int is_build_in(char *cmd, size_t *idx);
void do_build_in(int index, char *args[]);
#endif