#include "block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "util.h"

block block_new(const char *const icon, const char *const command,
                const unsigned int interval, const int signal) {
    block block = {
        .icon = icon,
        .command = command,
        .interval = interval,
        .signal = signal,

        .output = {[0] = '\0'},
        .fork_pid = -1,
    };

    return block;
}

int block_init(block *const block) {
    if (pipe(block->pipe) != 0) {
        (void)fprintf(stderr,
                      "error: could not create a pipe for \"%s\" block\n",
                      block->command);
        return 1;
    }

    return 0;
}

int block_deinit(block *const block) {
    int status = close(block->pipe[READ_END]);
    status |= close(block->pipe[WRITE_END]);
    if (status != 0) {
        (void)fprintf(stderr, "error: could not close \"%s\" block's pipe\n",
                      block->command);
        return 1;
    }

    return 0;
}

int block_execute(block *const block, const uint16_t button) {
    // Ensure only one child process exists per block at an instance.
    if (block->fork_pid != -1) {
        return 0;
    }

    block->fork_pid = fork();
    if (block->fork_pid == -1) {
        (void)fprintf(
            stderr, "error: could not create a subprocess for \"%s\" block\n",
            block->command);
        return 1;
    }

    if (block->fork_pid == 0) {
        const int write_fd = block->pipe[WRITE_END];
        int status = close(block->pipe[READ_END]);

        if (button != 0) {
			unsigned int modifier = (button & 0x7f00U) >> 8;
			unsigned int button_ = (button & 0xffU);
            char button_str[4];
			button_str[1] = '\0';
			button_str[0] = modifier & 0x1 ? '1' : '0'; status |= setenv("BLOCK_SHIFT", button_str, 1);
			button_str[0] = modifier & 0x2 ? '1' : '0'; status |= setenv("BLOCK_CONTROL", button_str, 1);
			button_str[0] = modifier & 0x4 ? '1' : '0'; status |= setenv("BLOCK_MOD1", button_str, 1);
			button_str[0] = modifier & 0x8 ? '1' : '0'; status |= setenv("BLOCK_MOD2", button_str, 1);
			button_str[0] = modifier & 0x10 ? '1' : '0'; status |= setenv("BLOCK_MOD3", button_str, 1);
			button_str[0] = modifier & 0x20 ? '1' : '0'; status |= setenv("BLOCK_MOD4", button_str, 1);
			button_str[0] = modifier & 0x40 ? '1' : '0'; status |= setenv("BLOCK_MOD5", button_str, 1);
            (void)snprintf(button_str, LEN(button_str), "%02x", (uint8_t)modifier);
			status |= setenv("BLOCK_MODIFIERS", button_str, 1);
            (void)snprintf(button_str, LEN(button_str), "%hhu", button_);
            status |= setenv("BLOCK_BUTTON", button_str, 1);
        }

        const char null = '\0';
        if (status != 0) {
            (void)write(write_fd, &null, sizeof(null));
            exit(EXIT_FAILURE);
        }

        FILE *const file = popen(block->command, "r");
        if (file == NULL) {
            (void)write(write_fd, &null, sizeof(null));
            exit(EXIT_FAILURE);
        }

        // Ensure null-termination since fgets() will leave buffer untouched on
        // no output.
        char buffer[LEN(block->output)] = {[0] = null};
        (void)fgets(buffer, LEN(buffer), file);

        // Remove trailing newlines.
        const size_t length = strcspn(buffer, "\n");
        buffer[length] = null;

        // Exit if command execution failed or if file could not be closed.
        if (pclose(file) != 0) {
            (void)write(write_fd, &null, sizeof(null));
            exit(EXIT_FAILURE);
        }

        const size_t output_size =
            truncate_utf8_string(buffer, LEN(buffer), MAX_BLOCK_OUTPUT_LENGTH);
        (void)write(write_fd, buffer, output_size);

        exit(EXIT_SUCCESS);
    }

    return 0;
}

int block_update(block *const block) {
    char buffer[LEN(block->output)];

    const ssize_t bytes_read =
        read(block->pipe[READ_END], buffer, LEN(buffer));
    if (bytes_read == -1) {
        (void)fprintf(stderr,
                      "error: could not fetch output of \"%s\" block\n",
                      block->command);
        return 2;
    }

    // Collect exit-status of the subprocess to avoid zombification.
    int fork_status = 0;
    if (waitpid(block->fork_pid, &fork_status, 0) == -1) {
        (void)fprintf(stderr,
                      "error: could not obtain exit status for \"%s\" block\n",
                      block->command);
        return 2;
    }
    block->fork_pid = -1;

    if (fork_status != 0) {
        (void)fprintf(stderr,
                      "error: \"%s\" block exited with non-zero status\n",
                      block->command);
        return 1;
    }

    (void)strncpy(block->output, buffer, LEN(buffer));

    return 0;
}
