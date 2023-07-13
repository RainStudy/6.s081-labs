#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char* path, char* filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        exit(1);
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        exit(1);
    }

    // 路径必须是 dir 才行
    if (st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        exit(1);
    }

    // 这段是从 ls 抄过来的，遍历文件夹中所有文件
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", buf);
        continue;
      }
      // 如果文件名相同，就将其打印出来
      if (strcmp(p, filename) == 0) {
        printf("%s\n", buf);
      } else if(st.type == T_DIR && strcmp(p,".") !=0 && strcmp(p,"..") !=0){
        find(buf, filename); // 递归到这个文件夹里
      }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(2, "usage: find <PATH> <FILENAME>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}