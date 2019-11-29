#include "tools.h"
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <syslog.h>

void strip_space(char *str, size_t *length)
{
	assert(str != NULL);

	size_t i, spare_length;
	
	if (length == NULL) {
		spare_length = strlen(str);
		length = &spare_length;
	}
	/*strip space at tail*/
	for (i = *length - 1; isspace(str[i]); --i) {
		str[i] = '\0';
		(*length)--;
	}
	/*strip space at head*/
	for (i = 0; isspace(str[i]); ++i);
	for (size_t j = 0, k = i; k <= spare_length; ++j,++k)
		str[j] = str[k];
	*length = *length - i;
}

/*
 * according to delimiter, break command into array of pointers to string(subcommand or arguments)
 * if success
 *     return array of pointers to string,
 *         the last pointer in this array has the value NULL,
 *         caller should free the array after use
 *     *number will be number of pointers
 * if command is full of delimiter(e.g. all-space when space is delimiter) or error occurred during parsing
 *     return NULL
 *     *number will be 0
 */
char **split_cmd(char *cmd_buf, const char *delimiter, size_t *number)
{
	assert(cmd_buf != NULL && delimiter != NULL);

	char **args;
	size_t arg_num = 0;

	args = calloc(strlen(cmd_buf) / 2 + 1, sizeof(char *)); //allocate absolutely sufficient memory
	if (args == NULL) {
		syslog(LOG_ERR, "Can't allocate cmd_buf: %m");
		exit(EXIT_FAILURE);
	}

	args[0] = strtok(cmd_buf, delimiter);
	if (args[0] == NULL) { //command is empty or full of delimiter
		free(args);
		if (number)
			*number = 0;
		return NULL;
	}
	arg_num++;
	for (size_t i = 1; (args[i] = strtok(NULL, delimiter)) != NULL; i++, arg_num++);

	args = realloc(args, (arg_num + 1) * sizeof(char *));
	args[arg_num] = NULL;
	if (number)
		*number = arg_num;
	return args;
}

