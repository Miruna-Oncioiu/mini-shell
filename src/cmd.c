// SPDX-License-Identifier: BSD-3-Clause

#include "cmd.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#define READ 0
#define WRITE 1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir) {
	/* TODO: Execute cd. */
	if (dir == NULL)
		return false;

	char *path = get_word(dir);

	if (chdir(path) == -1) {
		free(path);
		return false;
	}

	free(path);
	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void) {
	/* TODO: Execute exit/quit. */
	return SHELL_EXIT;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father) {
	/* TODO: Sanity checks. */

	/* TODO: If builtin command, execute the command. */

	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */

	char *command = get_word(s->verb);

	if (strcmp(command, "cd") == 0) {
		bool success = shell_cd(s->params);

		free(command);
		if (success == true)
			return 0;
		else
			return 1;
	}

	if (strchr(command, '=') != NULL) {
		putenv(command);
		return 0;
	}

	if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
		free(command);
		return shell_exit();
	}

	int pid = fork();

	if (pid == 0) {	 // Child process
		int argc, fd;
		char **argv = get_argv(s, &argc);

		if (s->in) {
			char *input_file = get_word(s->in);
			fd = open(input_file, O_RDONLY);
			DIE(fd < 0, "open");
			dup2(fd, STDIN_FILENO);
			free(input_file);
		}
		char *output_file = NULL, *error_file = NULL;

		if (s->out)
			output_file = get_word(s->out);
		if (s->err)
			error_file = get_word(s->err);
		if (output_file && error_file && strcmp(output_file, error_file) == 0) {
			if (s->io_flags & O_APPEND)
				fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
			else
				fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			DIE(fd < 0, "open");
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			free(output_file);
			free(error_file);
		} else {
			if (output_file) {
				if (s->io_flags && IO_OUT_APPEND)
					fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
				else
					fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				DIE(fd < 0, "open");
				dup2(fd, STDOUT_FILENO);
				free(output_file);
			}
			if (error_file) {
				if (IO_ERR_APPEND)
					fd = open(error_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
				else
					fd = open(error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				DIE(fd < 0, "open");
				dup2(fd, STDERR_FILENO);
				free(error_file);
			}
		}
		close(fd);
		execvp(argv[0], argv);
		fprintf(stderr, "Execution failed for '%s'\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	int status;

	waitpid(pid, &status, 0);
	free(command);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	else
		return -1;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
							command_t *father) {
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	// First child for cmd1
	int status1, status2, pid1 = fork();

	if (pid1 == 0)
		exit(parse_command(cmd1, level + 1, father));

	int pid2 = fork();

	if (pid2 == 0)
		exit(parse_command(cmd2, level + 1, father));

	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);
	if (WEXITSTATUS(status1) != 0 || WEXITSTATUS(status2) != 0)
		return false;
	return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
						command_t *father) {
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */
	int pipefd[2], status1, status2;

	DIE(pipe(pipefd) == -1, "pipe");
	int pid1 = fork();

	if (pid1 == 0) {
		// In child 1: Redirect stdout to write end of pipe
		close(pipefd[0]);  // Close unused read end
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		exit(parse_command(cmd1, level + 1, father));
	}

	int pid2 = fork();

	if (pid2 == 0) {
		// In child 2: Redirect stdin to read end of pipe
		close(pipefd[1]);  // Close unused write end
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		exit(parse_command(cmd2, level + 1, father));
	}

	// In parent: Close pipe ends and wait for children
	close(pipefd[0]);
	close(pipefd[1]);
	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);
	return WEXITSTATUS(status2);
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father) {
	/* TODO: sanity checks */
	if (c->op == OP_NONE)
		/* TODO: Execute a simple command. */
		return parse_simple(c->scmd, level, father);
	switch (c->op) {
		case OP_SEQUENTIAL:
			/* TODO: Execute the commands one after the other. */
			parse_command(c->cmd1, level + 1, c);
			return parse_command(c->cmd2, level + 1, c);

		case OP_PARALLEL:
			/* TODO: Execute the commands simultaneously. */
			return run_in_parallel(c->cmd1, c->cmd2, level, father);

		case OP_CONDITIONAL_NZERO:
			/* TODO: Execute the second command only if the first one
			 * returns non zero.
			 */
			if (parse_command(c->cmd1, level + 1, c) != 0)
				return parse_command(c->cmd2, level + 1, c);
			return 0;

		case OP_CONDITIONAL_ZERO:
			/* TODO: Execute the second command only if the first one
			 * returns zero.
			 */
			// Execută cmd1, verifică exit status-ul
			int status = parse_command(c->cmd1, level + 1, c);
			// Dacă status-ul este zero, execută cmd2
			if (status == 0)
				return parse_command(c->cmd2, level + 1, c);
			return 1;  // Returnează status-ul comenzii inițiale

		case OP_PIPE:
			/* TODO: Redirect the output of the first command to the
			 * input of the second.
			 */
			return run_on_pipe(c->cmd1, c->cmd2, level, c);

		default:
			return SHELL_EXIT;
	}
	return 0; /* TODO: Replace with actual exit code of command. */
}
