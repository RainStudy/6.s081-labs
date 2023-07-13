#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 思路参考 https://swtch.com/~rsc/thread/
// 每个进程负责筛掉 n 的倍数
// 由于 xv6 文件描述符数量有限，所以要及时关闭打开的 pipe
// 父进程和子进程中都得关闭，因为我们知道 fd 的 close 是引用计数式的
// 感觉很像 fork-join 模型啊
int fork_calc(int n, int* pipes) {

    printf("prime %d\n", n);

    int* curr = malloc(sizeof(int));
    int next_n = 0;
    int p[2];
    pipe(p);
    // 一个一个读上个进程发送过来的数据
    while (read(pipes[0], curr, sizeof(int))) {
        int tmp = *curr;
        if (tmp == n) continue;
        int filtered = 0;
        for (int i = n * n; i <= 35; i += n) {
            if (tmp == i) {
                filtered = 1;
                break;
            }
        }
        if (!filtered) {
            if (next_n == 0) {
                next_n = tmp;
            }
            write(p[1], curr, sizeof(int));
        }
    }
    free(curr);
    // 关闭写端
    close(p[1]);

    // 存在下一个进程时才进行fork，如果已经没有数据了就不fork了
    // 其实这里也可以不 fork 直接递归算，以前用 go 写过一个支持并发的文件查找
    // 设定一个 max_worker_count，然后如果当前 worker 数量大于等于这个值
    // 就在当前协程继续递归，反之才开启新协程，这里的道理也是一样.
    // 只是 pipe 大概比 channel 昂贵多了
    int pid;
    if (next_n && fork() == 0) {
        fork_calc(next_n, p);
    } else {
        // 等待子进程结束
        wait(&pid);
    }
    // 读端 fd 在子进程和父进程都得关闭一次才能彻底释放
    close(p[0]);
    exit(0);
}

int main() {
    int p[2];
    pipe(p);

    // 主进程负责发送 2～35
    for (int i = 2; i <= 35; i++) {
        write(p[1], &i, sizeof(int));
    }
    close(p[1]);

    if (fork() == 0) {
        fork_calc(2, p);
        exit(0);
    } else {
        close(p[0]);
        int pid;
        // 等待子进程结束
        wait(&pid);
        exit(0);
    }
}