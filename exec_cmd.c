#define _GNU_SOURCE
#include "exec_cmd.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include "build_in.h"
#include "signal_handler.h"
#include "sh_env.h"
#include "tools.h"

static pid_t execute_single_cmd(char **args, int bg)
{
	assert(args != NULL && args[0] != NULL);

	char *cmd = args[0];
	pid_t job_id = 0;
	size_t buildin_idx;

	if (is_build_in(cmd, &buildin_idx)) {
		do_build_in(buildin_idx, args);
	} else {
		if ((job_id = fork()) < 0) {
			syslog(LOG_ERR, "Can't fork: %m");
			return 0;
		} else if (job_id == 0) {
			if (setpgid(0, 0) != 0) {
				syslog(LOG_ERR, "Can't create pgrp: %m");
				_exit(EXIT_FAILURE);
			}
			if (!bg && tcsetpgrp(STDIN_FILENO, getpid()) != 0) {
				syslog(LOG_ERR, "Can't hand over terminal to child: %m");
				_exit(EXIT_FAILURE);
			}
			reset_sig_process();
			execvp(cmd, args);
			perror(cmd);
			_exit(127);
		}
	}

	return job_id;
}

pid_t execute_cmd(char **cmd_list, const size_t last_idx, size_t cur_idx, int out_fd, int bg)
{
	assert(cmd_list != NULL && last_idx >= 0 && cur_idx >= 0);
	char **args = NULL, *cmd = NULL, *redict_file = NULL;
	int redict_mode, pipe_fd[2];
	int save_stdout = dup(STDOUT_FILENO);
	size_t buildin_idx;
	pid_t jobid = 0, child_pid = 0;

	/*parse command*/
	for (size_t i = 0; cmd_list[cur_idx][i] != '\0'; ++i) {
		if (cmd_list[cur_idx][i] == '>') {
			if (cmd_list[cur_idx][i + 1] == '>') 
				redict_mode = O_CREAT | O_WRONLY | O_APPEND;
			else
				redict_mode = O_CREAT | O_WRONLY | O_TRUNC;
			break;
		}
	}
	if ((args = split_cmd(cmd_list[cur_idx], ">", NULL)) == NULL)
		return 0;
	cmd = args[0];
	redict_file = args[1];
	if (redict_file != NULL && args[2] == NULL) {
		int redict_fd;
		strip_space(redict_file, NULL);
		if ((redict_fd = open(redict_file, redict_mode, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH)) == -1) {
			fprintf(stderr, "Can't redirect to %s: %s\n", redict_file, strerror(errno));
			return 0;
		}
		out_fd = redict_fd;
	} else if (redict_file != NULL && args[2] != NULL) {
		fprintf(stderr, "Redirection syntax error\n");
		return 0;
	}

	if ((args = split_cmd(cmd, " \t\n", NULL)) == NULL)
		return 0;
	cmd = args[0];

	dup2(out_fd, STDOUT_FILENO);
	/*no pipe, single command*/
	if (last_idx == 0) {
		jobid = execute_single_cmd(args, bg);
		dup2(save_stdout, STDOUT_FILENO);
		close(save_stdout);
		return jobid;
	}

	/*code from here to end of function are use to do pipe job*/
	if (last_idx == cur_idx) {
		int pipe_tc[2];
		pipe2(pipe_tc, O_CLOEXEC);
		pipe2(pipe_fd, O_CLOEXEC);
		/*fork and execute end process*/
		if ((jobid = fork()) < 0) {
			syslog(LOG_ERR, "Can't fork: %m");
			_exit(127); 
		} else if (jobid == 0) {
			if (setpgid(0, 0) != 0) {
				syslog(LOG_ERR, "Can't create pgrp: %m");
				_exit(EXIT_FAILURE);
			}
			if (!bg && tcsetpgrp(STDIN_FILENO, getpid()) != 0) {
				syslog(LOG_ERR, "Can't hand over terminal to child: %m");
				_exit(EXIT_FAILURE);
			}
			write(pipe_tc[1], "y", 1);
			dup2(pipe_fd[0], STDIN_FILENO);
			reset_sig_process();
			execvp(cmd, args);
			perror(cmd);
			_exit(127);
		}
		/*fork and execute previous process*/
		if ((child_pid = fork()) < 0) {
			syslog(LOG_ERR, "Can't fork: %m");
			_exit(127);                       
		} else if (child_pid == 0) {
			char tc_stat;
			if ((read(pipe_tc[0], &tc_stat, 1) == 0 || tc_stat != 'y' || setpgid(0, jobid) != 0)) {
				syslog(LOG_ERR, "Can't move child to pgrd: %m");
				_exit(EXIT_FAILURE);
			}
			execute_cmd(cmd_list, last_idx, cur_idx - 1, pipe_fd[1], bg);
			_exit(127);
		}
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		close(pipe_tc[0]);
		close(pipe_tc[1]);
		dup2(save_stdout, STDOUT_FILENO);
		return jobid;
	} else if (cur_idx != 0) { //this is one of middle processes in pipe job
		pipe2(pipe_fd, O_CLOEXEC);
		if ((child_pid = fork()) < 0) {
			syslog(LOG_ERR, "Can't fork: %m");
			_exit(127);
		} else if (child_pid == 0) {
			execute_cmd(cmd_list, last_idx, cur_idx - 1, pipe_fd[1], bg);
			_exit(127);
		}
		dup2(pipe_fd[0], STDIN_FILENO);
		if (is_build_in(cmd, &buildin_idx)) {
			do_build_in(buildin_idx, args);
		} else {
			reset_sig_process();
			execvp(cmd, args);
			perror(cmd);
		}
		_exit(127);
	} else { //this is the start process in pipe job
		if (is_build_in(cmd, &buildin_idx)) {
			do_build_in(buildin_idx, args);
		} else {
			reset_sig_process();
			execvp(cmd, args);
			perror(cmd);
		}
		_exit(127);
	}

	return 0;//shouldn't reach here
}

void do_cmd(const char *input_cmd)
{
	assert(input_cmd != NULL);

	size_t input_cmd_len, cmd_count, last_cmd_idx;
	sigset_t oldmask, wait_chld_mask, allmask;
	struct job_state job;
	char *cmd = NULL, **pipe_cmds = NULL;
	int bg = 0;

	if ((input_cmd_len = strlen(input_cmd)) == 0)
		return;
	if ((cmd = malloc(input_cmd_len + 1)) == NULL) {
		syslog(LOG_ERR, "Can't allocate command parse buffer: %m");
		exit(EXIT_FAILURE);
	}
	strcpy(cmd, input_cmd);
	if (cmd[input_cmd_len - 1] == '&') {
		bg = 1;
		cmd[--input_cmd_len] = '\0';
	}


	pipe_cmds = split_cmd(cmd, "|", &cmd_count);
	if (pipe_cmds == NULL)
		goto free_and_return; //command is empty or full of '|'
	last_cmd_idx = cmd_count - 1;

	sigfillset(&wait_chld_mask);
	sigfillset(&allmask);
	sigdelset(&wait_chld_mask, SIGCHLD);
	sigprocmask(SIG_SETMASK, &allmask, &oldmask);
	job.pgid = execute_cmd(pipe_cmds, last_cmd_idx, last_cmd_idx, STDOUT_FILENO, bg);

	if (job.pgid != 0 && bg == 0) {
		set_fg_job(job.pgid, input_cmd);
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
	} else if (job.pgid != 0) {
		set_bg_job(job.pgid, input_cmd, BG_ADD);
	}
	sigprocmask(SIG_SETMASK, &oldmask, NULL);

free_and_return:
	if (pipe_cmds)
		free(pipe_cmds);
	if (cmd)
		free(cmd);
	return;
}
