#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
    int time = 0;
    if (argc <= 1) {
        fprintf(2, "usage: sleep [time]\n");
        exit(0);
    }
    time = atoi(argv[1]);
    sleep(time);
    exit(0);
}