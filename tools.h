#ifndef NSPT_TOOLS
#define NSPT_TOOLS

#include <stddef.h>

void strip_space(char *str, size_t *length);
char **split_cmd(char *cmd_buf, const char *delimiter, size_t *number);

#endif
