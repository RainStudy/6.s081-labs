#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        char ping[5];
        // 子进程先读 ping
        read(p[0], ping, 5);
        printf("%d: received %s\n", getpid(), ping);
        // 关闭读端
        close(p[0]);
        // 写 pong
        write(p[1], "pong", 5);
        // 关闭写端
        close(p[1]);
        exit(0);
    } else {
        char pong[5];
        // 父进程先写一个ping
        write(p[1], "ping", 5);
        // 关闭写端
        close(p[1]);
        // 读 pong
        read(p[0], pong, 5);
        printf("%d: received %s\n", getpid(), pong);
        // 关闭读端
        close(p[0]);
        exit(0);
    }
}