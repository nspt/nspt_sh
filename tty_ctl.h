#ifndef NSPT_TTY_CTL
#define NSPT_TTY_CTL

#include <stddef.h>

size_t get_cmd(char *cmd_buf, size_t buf_length, int *err);
void tty_init();
void tty_reset();
void tty_cbreak();

#endif
