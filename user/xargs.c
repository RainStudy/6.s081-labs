#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define BUF_SIZE 100

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        fprintf(2, "usage: xargs [command...]\n");
        exit(1);
    }

    char* cmd_args[MAXARG];
    int cmd_args_len = argc - 1;

    for (int i = 1; i < argc; i++) {
        cmd_args[i - 1] = argv[i];
    }

    int index = 0;
    char buf[BUF_SIZE];
    char byte;

    // 从 stdin 里面读
    while (read(0, &byte, 1) > 0) {
        if (index >= BUF_SIZE - 1) {
            fprintf(2, "xargs: argument too long\n");
            exit(1);
        }

        buf[index++] = byte;

        if (byte == ' ' || byte == '\n') {
            buf[index - 1] = '\0';  // Replace space with null terminator

            char* param_buf = malloc(sizeof(char) * (index + 1));
            if (param_buf == 0) {
                fprintf(2, "xargs: out of memory\n");
                exit(1);
            }

            strcpy(param_buf, buf);

            if (cmd_args_len >= MAXARG - 1) {
                fprintf(2, "xargs: too many arguments\n");
                exit(1);
            }

            cmd_args[cmd_args_len++] = param_buf;
            index = 0;
        }
    }

    cmd_args[cmd_args_len] = 0;  // End of arguments

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "xargs: fork failed\n");
        exit(1);
    } else if (pid == 0) {
        if (exec(cmd_args[0], cmd_args) < 0) {
            fprintf(2, "xargs: exec failed\n");
            exit(1);
        }
    } else {
        wait(0);
    }

    // Free allocated memory
    for (int i = argc - 1; i < cmd_args_len; i++) {
        free(cmd_args[i]);
    }

    exit(0);
}
