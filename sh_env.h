#ifndef NSPT_SH_ENV
#define NSPT_SH_ENV

#include <stddef.h>
#include <sys/types.h>

struct job_state {
	pid_t pgid;
	char state;
};

void env_init();
size_t get_input_cmd(const char **input_cmd, int *err);
void update_cwd();
const char *get_home_dir();
int is_bgpgid(pid_t pgid, size_t *index);
int update_job_state(int output, struct job_state *interest, size_t length);
void set_fg_job(pid_t pgid, const char *cmd);
void set_bg_job(pid_t pgid, const char *cmd, int option);
void fg2bg();
void output_jobs();
int bg2fg(pid_t pgid);

/* this pipe is use for communication between SIGCHLD handler and normal process
 * SIGCHLD handler write child process(group) status to sigchld_handler_pipe[1]
 *     format: a pid_t:      child pid(equal to pgid and job id)
 *     followed by one char: 'e' is exit, 's' is stop, 'c' is continue
 *     followed by a int:    child exit code(if child terminated)
 * normal process will try to read sigchld_handler_pipe[0] in update_job_state()
 */
extern int sigchld_handler_pipe[2];
#define BG_ADD 0
#define BG_RM  1
#define SET_ECODE 1
#define GET_ECODE 0

#endif
