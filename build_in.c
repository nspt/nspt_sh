#define _GNU_SOURCE
#include "build_in.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <signal.h>
#include "exec_cmd.h"
#include "sh_env.h"
#include "tools.h"
#include "tty_ctl.h"

static int build_in_cd(char **argv);
static int build_in_type(char **argv);
static int build_in_jobs(char **argv);
static int build_in_fg(char **argv);
static int build_in_bg(char **argv);
static int build_in_exit(char **argv);

struct buildin {
	char *cmd;
	int (*func)(char **argv);
};

static struct buildin build_in_cmds[] = {
	{"cd", build_in_cd},
	{"type", build_in_type},
	{"jobs", build_in_jobs},
	{"fg", build_in_fg},
	{"bg", build_in_bg},
	{"exit", build_in_exit}
};

int is_build_in(char *cmd, size_t *idx)
{
	assert(cmd != NULL);
	for (size_t i = 0; i < sizeof(build_in_cmds)/sizeof(struct buildin); ++i) {
		if (strcmp(build_in_cmds[i].cmd, cmd) == 0) {
			if (idx)
				*idx = i;
			return 1;
		}
	}
	return 0;
}

void do_build_in(int index, char *args[])
{
	assert(index >= 0 && index < sizeof(build_in_cmds)/sizeof(struct buildin));
	assert(args != NULL && args[0] != NULL);
	build_in_cmds[index].func(args);
}

static int build_in_exit(char **argv)
{
	exit(EXIT_SUCCESS);
}

static int build_in_cd(char **argv)
{
	const char *target_dir;

	if (argv[1] != NULL && argv[2] != NULL) {
		fprintf(stderr, "cd: Too many arguments\n");
		return -1;
	}
	target_dir = (argv[1] != NULL) ? argv[1] : get_home_dir();
	if (chdir(target_dir) != 0) {
		fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(errno));
		return -1;
	}
	update_cwd();
	return 0;
}

static int build_in_type(char **argv)
{
	char *cmd, *which_cmd;
	FILE *which_pipe;
	int which_output;

	for(size_t i = 1; (cmd = argv[i]) != NULL; i++) {
		if (is_build_in(cmd, NULL)) {
			printf("%s: shell buildin\n", cmd);
			continue;
		}
		which_cmd = malloc(strlen("which ") + strlen(cmd) + 1); //+1 for '\0'
		if (which_cmd == NULL) {
			syslog(LOG_ERR, "type: can't allocate memory: %m");
			return -1;
		}
		strcpy(which_cmd, "which ");
		strcat(which_cmd, cmd);
		which_pipe = popen(which_cmd, "r");
		free(which_cmd);
		if ((which_output = fgetc(which_pipe)) != EOF) {
			if (which_output == '/') {
				printf("%s: ", cmd);
				do {
					putchar(which_output);
				} while((which_output = fgetc(which_pipe)) != EOF);
			}
		} else {
			printf("%s: not found\n", cmd);
		}
		pclose(which_pipe);
	}
	return 0;
}

static int build_in_jobs(char **argv)
{
	output_jobs();
	return 0;
}

static int build_in_fg(char **argv)
{
	struct job_state job;
	sigset_t oldmask, wait_chld_mask, allmask;

	if (argv[1] == NULL) {
		fprintf(stderr, "fg: usage: fg <job_id>\n");
		return -1;
	}
	job.pgid = (pid_t)atoll(argv[1]);
	job.pgid = job.pgid < 0 ? -job.pgid : job.pgid;

	sigfillset(&wait_chld_mask);
	sigfillset(&allmask);
	sigdelset(&wait_chld_mask, SIGCHLD);
	sigprocmask(SIG_SETMASK, &allmask, &oldmask);
	if (!bg2fg(job.pgid)) {
		fprintf(stderr, "fg: no such job\n");
		return -1;
	} else if (tcsetpgrp(STDIN_FILENO, job.pgid) != 0) {
		syslog(LOG_ERR, "Can't hand over terminal to job: %lu :%m", (unsigned long)job.pgid);
	}
	tty_reset();
	kill(-job.pgid, SIGCONT);
	while (1) {
		sigsuspend(&wait_chld_mask);
		update_job_state(0, &job, 1);
		if (job.state == 'e' || job.state == 's') {
			break;
		}
	}
	if (tcsetpgrp(STDIN_FILENO, getpid()) != 0) {
		syslog(LOG_ERR, "Can't hand over terminal to parent: %m");
		exit(EXIT_FAILURE);
	}
	tty_cbreak();
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	return 0;
}

static int build_in_bg(char **argv)
{
	pid_t pgid;
	if (argv[1] == NULL) {
		fprintf(stderr, "bg: usage: bg <pgid>\n");
		return -1;
	}
	pgid = (pid_t)atoll(argv[1]);
	pgid = pgid < 0 ? -pgid : pgid;
	if (!is_bgpgid(pgid, NULL)) {
		fprintf(stderr, "bg: no such job\n");
		return -1;
	}
	kill(-pgid, SIGCONT);
	return 0;
}
