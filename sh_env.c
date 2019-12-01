#define _GNU_SOURCE
#include "sh_env.h"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <ctype.h>
#include "exec_cmd.h"
#include "signal_handler.h"
#include "tools.h"

int sigchld_handler_pipe[2] = {-1, -1};

#define PATH_MAX_LEN_GUESS    1024
#define BG_LIST_ORIG_MAX      10

struct job_info {
	char state;
	int output_state;
	pid_t pgid;
	const char *cmd;
};

static struct job_info exited_job = {'e', 0, 0, NULL};

static struct nspt_sh_env {
	char *cwd;
	long cwd_len_max;
	char *prompt_format;
	struct utsname sys_info;
	struct passwd user_info;
	struct job_info fg_job;
	struct job_info *bg_jobs;
	size_t bg_count, bg_max;
} *sh_env = NULL;

static void init_job_ctl()
{
	assert(sh_env != NULL && sh_env->bg_jobs == NULL);

	sh_env->bg_count = 0;
	sh_env->bg_max = BG_LIST_ORIG_MAX;
	sh_env->bg_jobs = malloc(BG_LIST_ORIG_MAX * sizeof(struct job_info));
	if (sh_env->bg_jobs == NULL) {
		syslog(LOG_ERR, "Can't allocate background processes list: %m");
		exit(EXIT_FAILURE);
	}

	sh_env->fg_job.pgid = 0;
	sh_env->fg_job.cmd = NULL;
}

static void init_cwd_buf()
{
	assert(sh_env != NULL
		&& sh_env->cwd == NULL);

	/*Init current working dir(cwd) buffer*/
	errno = 0;
	if ((sh_env->cwd_len_max = pathconf("/", _PC_PATH_MAX)) == -1) {
		if (errno != 0) {
			syslog(LOG_ERR, "Can't get limit: _PC_PATH_MAX: %m");
			exit(EXIT_FAILURE);
		}
		sh_env->cwd_len_max = PATH_MAX_LEN_GUESS;
	}
	if ((sh_env->cwd = malloc(sh_env->cwd_len_max)) == NULL) {
		syslog(LOG_ERR, "Can't allocate cwd buffer: %m");
		exit(EXIT_FAILURE);
	}
}

static void init_user_info()
{
	assert(sh_env != NULL);

	errno = 0;
	struct passwd *tmp = getpwuid(getuid());
	if (tmp == NULL) {
		syslog(LOG_ERR, "Can't get user info: %m");
		exit(EXIT_FAILURE);
	}
	memcpy(&sh_env->user_info, tmp, sizeof(struct passwd));
}

static void init_sys_info()
{
	assert(sh_env != NULL);

	if (uname(&sh_env->sys_info) != 0) {
		syslog(LOG_ERR, "Can't get system info: %m");
		exit(EXIT_FAILURE);
	}
}

void env_init()
{
	assert(sh_env == NULL);

	openlog("nspt_sh", LOG_PERROR, LOG_USER);

	if ((sh_env = malloc(sizeof(struct nspt_sh_env))) == NULL) {
		syslog(LOG_ERR, "Can't allocate env:%m");
		exit(EXIT_FAILURE);
	}

	init_job_ctl();
	init_cwd_buf();
	init_user_info();
	init_sys_info();
	set_sig_process();

	//format equal to "%s@%s:%s$ " --> "username@hostname:cwd$ "
	//color specification see: ascii escape sequences
	sh_env->prompt_format = "\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m$ ";

	setpgid(0, 0);
	if (tcsetpgrp(STDIN_FILENO, getpid()) != 0) {
		syslog(LOG_ERR, "Can't be foreground process group leader: %m");
		exit(EXIT_FAILURE);
	}

	do_cmd("cd");
}

int is_bgpgid(pid_t pgid, size_t *index)
{
	assert(sh_env != NULL && sh_env->bg_jobs != NULL);

	int finded = 0;
	for (size_t i = 0; i < sh_env->bg_count; ++i) {
		if (sh_env->bg_jobs[i].pgid == pgid) {
			if (index != NULL)
				*index = i;
			finded = 1;
			break;
		}
	}
	return finded;
}

void update_cwd()
{
	assert(sh_env != NULL && sh_env->cwd != NULL);

	errno = 0;
	while ((sh_env->cwd = getcwd(sh_env->cwd, sh_env->cwd_len_max)) == NULL) {
		if (errno == ERANGE) {
			sh_env->cwd_len_max *= 2;
			sh_env->cwd = realloc(sh_env->cwd, sh_env->cwd_len_max);
			if (sh_env->cwd == NULL) {
				syslog(LOG_ERR, "Can't realloc cwd buffer: %m");
				exit(EXIT_FAILURE);
			}
			errno = 0;
			continue;
		}
		syslog(LOG_ERR, "Can't get current working dir: %m");
		exit(EXIT_FAILURE);
	}

	if (strstr(sh_env->cwd, sh_env->user_info.pw_dir) == sh_env->cwd) {
		int cpy_idx = strlen(sh_env->user_info.pw_dir);
		sh_env->cwd[0] = '~';
		for (size_t i = 1; 1; ++cpy_idx, ++i) {
			sh_env->cwd[i] = sh_env->cwd[cpy_idx];
			if (sh_env->cwd[cpy_idx] == '\0')
				break;
		}
	}
}

const char *get_home_dir()
{
	assert(sh_env != NULL);

	return sh_env->user_info.pw_dir;
}

/* update job control information
 * parameters:
 *     output:   if it is not zero, job state change information will output to stdout
 *     interest: a list contains jobs we are interest, if a job specified in this list has changed state,
 *               it's state will set to the new state, otherwise set to 0
 *     length:   the number of members in interst list
 * return:
 *     if there are job changed state, return non-zero, otherwise return 0
 */
int update_job_state(int output, struct job_state *interest, size_t length)
{
	pid_t pgid = 0;
	char state;
	int ecode, read_ret; //ecode is temporary useless
	size_t bg_index;
	struct job_state *interest_child;

	for (size_t i = 0; i < length; ++i) {
		interest[i].state = 0;
	}

	for (errno = 0, interest_child = NULL;
	(read_ret = read(sigchld_handler_pipe[0], &pgid, sizeof(pid_t))) > 0;
	interest_child = NULL) {
		for (size_t i = 0; i < length; ++i) {
			if (interest[i].pgid == pgid)
				interest_child = &interest[i];
		}
		if ((read_ret = read(sigchld_handler_pipe[0], &state, sizeof(char))) <= 0) {
			syslog(LOG_ERR, "read child state from sigchld_handler_pipe returned: %d: %m", read_ret);
			exit(EXIT_FAILURE);
		}
		if (interest_child)
			interest_child->state = state == 'c' ? 'r' : state;
		if (state == 'e') { //child exited
			if ((read_ret = read(sigchld_handler_pipe[0], &ecode, sizeof(int))) <= 0) {
				syslog(LOG_ERR, "read child exit code from sigchld_handler_pipe returned: %d: %m", read_ret);
				exit(EXIT_FAILURE);
			}
			if (sh_env->fg_job.pgid == pgid) {
				set_fg_job(0, NULL);
			} else if (is_bgpgid(pgid, &bg_index)) {
				sh_env->bg_jobs[bg_index].state = state;
				sh_env->bg_jobs[bg_index].output_state = 1;
			}
		} else if (state == 's') { //child stoped
			if (sh_env->fg_job.pgid == pgid) {
				sh_env->fg_job.state = state;
				sh_env->fg_job.output_state = 1;
				fg2bg();
			} else if (is_bgpgid(pgid, &bg_index)) {
				sh_env->bg_jobs[bg_index].state = state;
				sh_env->bg_jobs[bg_index].output_state = 1;
			}
		} else if (state == 'c') { //child continued
			if (is_bgpgid(pgid, &bg_index)) {
				sh_env->bg_jobs[bg_index].state = 'r';
				sh_env->bg_jobs[bg_index].output_state = 1;
			}
		}
	}

	if (errno != 0 && errno != EAGAIN && errno != EAGAIN) {
		syslog(LOG_ERR, "read pid from sigchld_handler_pipe failed: %m");
		exit(EXIT_FAILURE);
	}

	if (output) {
		for (size_t i = 0; i < sh_env->bg_count; ++i) {
			if (!sh_env->bg_jobs[i].output_state)
				continue;
			sh_env->bg_jobs[i].output_state = 0;
			switch (sh_env->bg_jobs[i].state) {
				case 's':
					printf("%lu\t %s\t stoped\n", (unsigned long)sh_env->bg_jobs[i].pgid, sh_env->bg_jobs[i].cmd);
					break;
				case 'r':
					printf("%lu\t %s\t running\n", (unsigned long)sh_env->bg_jobs[i].pgid, sh_env->bg_jobs[i].cmd);
					break;
				case 'e':
					printf("%lu\t %s\t exited\n", (unsigned long)sh_env->bg_jobs[i].pgid, sh_env->bg_jobs[i].cmd);
					free((void *)sh_env->bg_jobs[i].cmd);
					sh_env->bg_jobs[i--] = sh_env->bg_jobs[sh_env->bg_count - 1]; //i-- because the last job hasn't handle
					sh_env->bg_count--;
					break;
			}
		}
	}
	return pgid == 0 ? 0 : 1;
}

void set_bg_job(pid_t pgid, const char *cmd, int option)
{
	struct job_info *bg = sh_env->bg_jobs;
	size_t *count = &(sh_env->bg_count), *max = &(sh_env->bg_max);
	if (option == BG_ADD) {
		char *tmp = malloc(strlen(cmd) + 1);
		if (tmp == NULL) {
			syslog(LOG_ERR, "Can't allocate fg_job cmd buffer: %m");
			exit(EXIT_FAILURE);
		}
		strcpy(tmp, cmd);
	
		if (*count == sh_env->bg_max) {
			*max *= 2;
			bg = realloc(bg, *max * sizeof(struct job_info));
			if (bg == NULL) {
				syslog(LOG_ERR, "Can't reallocate bg_jobs memory: %m");
				exit(EXIT_FAILURE);
			}
		}
		bg[*count].cmd = tmp;
		bg[*count].pgid = pgid;
		bg[*count].state = 'r';
		bg[*count].output_state = 1;
		(*count)++;
	} else if (option == BG_RM) {
		for (size_t i = 0; i < *count; ++i) {
			if (bg[i].pgid == pgid) {
				bg[i] = bg[*count - 1];
				(*count)--;
				break;
			}
		}
	}
}

void set_fg_job(pid_t pgid, const char *cmd)
{
	assert(sh_env != NULL);

	if (pgid == 0 || cmd == NULL) {
		if (sh_env->fg_job.cmd)
			free((void *)sh_env->fg_job.cmd);
		sh_env->fg_job = exited_job;
		return;
	}

	char *tmp;
	tmp = malloc(strlen(cmd) + 1);
	if (tmp == NULL) {
		syslog(LOG_ERR, "Can't allocate fg_job cmd buffer: %m");
		exit(EXIT_FAILURE);
	}
	strcpy(tmp, cmd);
	if (sh_env->fg_job.cmd)
		free((void *)sh_env->fg_job.cmd);
	sh_env->fg_job.cmd = tmp;
	sh_env->fg_job.pgid = pgid;
	sh_env->fg_job.state = 'r';
}

void fg2bg()
{
	assert(sh_env != NULL && sh_env->bg_jobs != NULL && sh_env->fg_job.cmd != NULL);

	struct job_info *fg = &(sh_env->fg_job);
	struct job_info *bg = sh_env->bg_jobs;
	size_t *count = &(sh_env->bg_count), *max = &(sh_env->bg_max);

	if (*count == sh_env->bg_max) {
		*max *= 2;
		bg = realloc(bg, *max * sizeof(struct job_info));
		if (bg == NULL) {
			syslog(LOG_ERR, "Can't reallocate bg_jobs memory: %m");
			exit(EXIT_FAILURE);
		}
	}
	bg[*count] = *fg;
	(*count)++;
	*fg = exited_job;
}

int bg2fg(pid_t pgid)
{
	assert(sh_env != NULL && sh_env->bg_jobs != NULL && sh_env->fg_job.cmd == NULL);

	int result = 0;

	set_fg_job(0, NULL);
	for (size_t i = 0; i < sh_env->bg_count; ++i) {
		if (sh_env->bg_jobs[i].pgid == pgid) {
			sh_env->fg_job = sh_env->bg_jobs[i];
			sh_env->bg_jobs[i] = sh_env->bg_jobs[sh_env->bg_count - 1];
			(sh_env->bg_count)--;
			result = 1;
			break;
		}
	}
	return result;
}

void output_jobs()
{
	for (size_t i = 0; i < sh_env->bg_count; ++i) {
		sh_env->bg_jobs[i].output_state = 0;
		switch (sh_env->bg_jobs[i].state) {
			case 's':
				printf("%lu\t %s\t stoped\n", (unsigned long)sh_env->bg_jobs[i].pgid, sh_env->bg_jobs[i].cmd);
				break;
			case 'r':
				printf("%lu\t %s\t running\n", (unsigned long)sh_env->bg_jobs[i].pgid, sh_env->bg_jobs[i].cmd);
				break;
			case 'e':
				printf("%lu\t %s\t exited\n", (unsigned long)sh_env->bg_jobs[i].pgid, sh_env->bg_jobs[i].cmd);
				free((void *)sh_env->bg_jobs[i].cmd);
				if (sh_env->bg_jobs[i].pgid != sh_env->bg_jobs[sh_env->bg_count - 1].pgid)
					sh_env->bg_jobs[i] = sh_env->bg_jobs[sh_env->bg_count - 1];
				sh_env->bg_count--;
				break;
		}
	}
}

void output_prompt()
{
	assert(sh_env != NULL);
	fprintf(stdout, sh_env->prompt_format, sh_env->user_info.pw_name, sh_env->sys_info.nodename, sh_env->cwd);
}
