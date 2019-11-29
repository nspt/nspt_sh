#include <syslog.h>
#include <stdlib.h>
#include "exec_cmd.h"
#include "sh_env.h"

int main(int argc, char *argv[])
{
	int read_err;
	const char *input_cmd;

	env_init();
	while(get_input_cmd(&input_cmd, &read_err) != 0) {
		if (input_cmd == NULL)
			continue;
		do_cmd(input_cmd);
	}

	if (read_err != 0) {
		syslog(LOG_ERR, "Can't read command from stdin: %m");
		exit(EXIT_FAILURE);
	}
	return 0;
}
