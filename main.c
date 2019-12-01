#include <syslog.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>
#include "exec_cmd.h"
#include "sh_env.h"
#include "tools.h"
#include "tty_ctl.h"

#define CMD_MAX_LEN_GUESS     2048
static char *cmd_buf = NULL;
static long cmd_buf_len;

static void cmd_buf_init()
{
	assert(cmd_buf == NULL);

	/*Init command buffer*/
	errno = 0;
	if ((cmd_buf_len = sysconf(_SC_LINE_MAX)) == -1) {
		if (errno != 0) {
			syslog(LOG_ERR, "Can't get limit: _SC_LINE_MAX: %m");
			exit(EXIT_FAILURE);
		}
		cmd_buf_len = CMD_MAX_LEN_GUESS;
	}
	if ((cmd_buf = malloc(cmd_buf_len)) == NULL) {
		syslog(LOG_ERR, "Can't allocate command buffer: %m");
		exit(EXIT_FAILURE);
	}
}

static void sh_init()
{
	cmd_buf_init();
	tty_init();
	env_init();
}

int main(int argc, char *argv[])
{
	int read_err;

	sh_init();
	while(1) {
		get_cmd(cmd_buf, cmd_buf_len, &read_err);
		if (read_err)
			break;
		do_cmd(cmd_buf);
	}
	return 0;
}
