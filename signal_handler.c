#define _GNU_SOURCE
#include "signal_handler.h"
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include "sh_env.h"


#include <stdio.h>

#define INT_STR_LEN_MAX(int_type) (sizeof(int_type) * 8 / 3 + 2) //enough length to hold string converted from int_type

static struct sigaction r_int_act, r_quit_act, r_ttou_act, r_chld_act, r_term_act, r_pipe_act, r_stop_act, r_tstp_act;
static sigset_t r_sig_mask;

static void sig_child(int signo)
{
	assert(sigchld_handler_pipe[0] != sigchld_handler_pipe[1]);

	pid_t chld_pid;
	int term_stat, exit_code;
	int olderr = errno;
	//note: chld_pid is also child pgid and job id, we guarantee that in do_cmd()
	while ((chld_pid = waitpid(-1, &term_stat, WCONTINUED | WNOHANG | WUNTRACED)) > 0) {
		write(sigchld_handler_pipe[1], &chld_pid, sizeof(pid_t));
		if (WIFEXITED(term_stat) || WIFSIGNALED(term_stat)) {
			exit_code = WIFEXITED(term_stat) ? WEXITSTATUS(term_stat) : WTERMSIG(term_stat) + 255;
			write(sigchld_handler_pipe[1], "e", 1);
			write(sigchld_handler_pipe[1], &exit_code, sizeof(int));
		}
		else if (WIFSTOPPED(term_stat)) {
			write(sigchld_handler_pipe[1], "s", 1);
		}
		else if (WIFCONTINUED(term_stat)) {
			write(sigchld_handler_pipe[1], "c", 1);
		}
	}
	errno = olderr;
}

void set_sig_process()
{
	assert(sigchld_handler_pipe[0] == sigchld_handler_pipe[1]);

	struct sigaction ign_act, chld_act;
	sigset_t empty_mask;

	if (pipe2(sigchld_handler_pipe, O_NONBLOCK) != 0) {
		syslog(LOG_ERR, "Can't create pipe: sigchld_handler_pipe: %m");
		exit(EXIT_FAILURE);
	}

	/*ignore SIGSTOP, SIGTSTP, SIGINT, SIGTERM, SIGQUIT, SIGTTOU, SIGPIPE*/
	ign_act.sa_handler = SIG_IGN;
	sigemptyset(&ign_act.sa_mask);
	ign_act.sa_flags = 0;
	sigaction(SIGTTOU, &ign_act, &r_ttou_act);
	sigaction(SIGINT, &ign_act, &r_int_act);
	sigaction(SIGQUIT, &ign_act, &r_quit_act);
	sigaction(SIGTERM, &ign_act, &r_term_act);
	sigaction(SIGPIPE, &ign_act, &r_pipe_act);
	sigaction(SIGSTOP, &ign_act, &r_stop_act);
	sigaction(SIGTSTP, &ign_act, &r_tstp_act);

	/*cat SIGCHLD*/
	chld_act.sa_handler = sig_child;
	sigfillset(&chld_act.sa_mask);
	chld_act.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &chld_act, &r_chld_act);

	/*unblock all mask*/
	sigemptyset(&empty_mask);
	sigprocmask(SIG_SETMASK, &empty_mask, &r_sig_mask);
}

void reset_sig_process()
{
	assert(sigchld_handler_pipe[0] != sigchld_handler_pipe[1]);

	sigaction(SIGTTOU, &r_ttou_act, NULL);
	sigaction(SIGINT, &r_int_act, NULL);
	sigaction(SIGQUIT, &r_quit_act, NULL);
	sigaction(SIGTERM, &r_term_act, NULL);
	sigaction(SIGCHLD, &r_chld_act, NULL);
	sigaction(SIGPIPE, &r_pipe_act, NULL);
	sigaction(SIGTSTP, &r_tstp_act, NULL);
	sigaction(SIGSTOP, &r_stop_act, NULL);
	sigprocmask(SIG_SETMASK, &r_sig_mask, NULL);
}
