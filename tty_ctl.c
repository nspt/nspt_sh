#include "tty_ctl.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>
#include "sh_env.h"
#include "tools.h"

#define KEY_TAB       9
#define KEY_BACKSPACE 127
#define KEY_ESCAPE    27
#define KEY_L_BRACKET 91
#define KEY_CTRL_D    4

static struct termios *save_term = NULL;

void tty_cbreak() /* put terminal into a cbreak mode */
{
	static int inited = 0;
	struct termios buf;

	if (!inited && (save_term = malloc(sizeof(struct termios))) == NULL) {
		syslog(LOG_ERR, "Can't allocate memory for save_term: %m");
		exit(EXIT_FAILURE);
	}

	if (tcgetattr(STDIN_FILENO, &buf) < 0) {
		syslog(LOG_ERR, "Can't get termios by tcgetattr: %m");
		exit(EXIT_FAILURE);
	}
	if (!inited)
		*save_term = buf;
	buf.c_lflag &= ~(ECHO | ICANON);
	buf.c_lflag &= ISIG;
	buf.c_iflag &= ~(IGNCR | INLCR);
	buf.c_iflag &= ICRNL | IXOFF | IXON;
	buf.c_cc[VMIN] = 1;
	buf.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &buf) != 0) {
		syslog(LOG_ERR, "Can't set termios by tcsetattr: %m");
		exit(EXIT_FAILURE);
	}

	if (!inited)
		inited = 1;
}


void tty_reset()
{
	assert(save_term != NULL);

	tcsetattr(STDIN_FILENO, TCSANOW, save_term);
}

static void remove_char(char *cmd_buf, size_t *cur_idx, size_t *end_idx)
{
	if (*cur_idx == 0)
		return;

	if (*cur_idx == *end_idx) {
		(*cur_idx)--;
		(*end_idx)--;
		fputs("\b \b", stdout);
		return;
	}

	size_t num_steps = 0;
	for (size_t i = *cur_idx; i < *end_idx; ++i) {
		cmd_buf[i - 1] = cmd_buf[i];
	}
	(*cur_idx)--;
	(*end_idx)--;
	putchar('\b');
	for (size_t i = *cur_idx; i < *end_idx; ++num_steps, ++i) {
		putchar(cmd_buf[i]);	
	}
	putchar(' ');
	++num_steps;
	for (size_t i = 0; i < num_steps; ++i) {
		putchar('\b');
	}
}

static void insert_char(char *cmd_buf, size_t buf_length, char ch, size_t *cur_idx, size_t *end_idx)
{
	if (*end_idx == buf_length - 1)
		return;

	if (*cur_idx == *end_idx) {
		putchar(ch);
		cmd_buf[*cur_idx] = ch;
		(*cur_idx)++;
		(*end_idx)++;
		return;
	}

	for (size_t i = *end_idx - 1; i >= *cur_idx; --i) {
		cmd_buf[i + 1] = cmd_buf[i];
	}
	cmd_buf[*cur_idx] = ch;
	(*end_idx)++;

	size_t num_steps = 0;
	for (size_t i = *cur_idx; i < *end_idx; ++num_steps, ++i) {
		putchar(cmd_buf[i]);	
	}
	(*cur_idx)++;
	for (size_t i = 0; i < num_steps - 1; ++i) {
		putchar('\b');
	}
}

size_t get_cmd(char *cmd_buf, size_t buf_length, int *err)
{
	size_t end_idx = 0, cur_idx = 0;
	int ch;

	*err = 0;
	output_prompt();
	while (1) {
		if ((ch = getchar()) == EOF || ch == KEY_CTRL_D) {
			if (end_idx == 0) {
				*err = 1;
				return 0;	
			} else 
				continue;
		}
		if (ch == KEY_TAB) {
			putchar('\a');
			continue;
		} //KEY_TAB
		if (ch == KEY_ESCAPE) {
			if ((ch = getchar()) != KEY_L_BRACKET)
				continue;
			switch (ch = getchar()) {
				case 'A':
				case 'B':
					putchar('\a');
					continue;
				case 'C':
					if (cur_idx < end_idx) {
						fputs("\e[C", stdout);
						cur_idx++;
					}
					continue;
				case 'D':
					if (cur_idx > 0) {
						fputs("\e[D", stdout);
						cur_idx--;
					}
					continue;
				default:
					break;
			}
		} //KEY_ESCAPE
		if (ch == KEY_BACKSPACE) {
			remove_char(cmd_buf, &cur_idx, &end_idx);
			continue;
		} //KEY_BACKSPACE

		if (ch == '\n') {
			putchar(ch);
			break;
		}
		if (isprint(ch)) {
			insert_char(cmd_buf, buf_length, (char)ch, &cur_idx, &end_idx);
		}
	}

	cmd_buf[end_idx] = '\0';
	return end_idx;
}

void tty_init()
{
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		syslog(LOG_ERR, "stdin or stdout is not a terminal");
		exit(EXIT_FAILURE);
	}
	tty_cbreak();
	atexit(tty_reset);
}
