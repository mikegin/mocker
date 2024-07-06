#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <dirent.h>
#include <sys/mount.h>
#include <err.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#define STACK_SIZE (1024 * 1024)  // Stack size for the child process

static void update_map(char *mapping, char *map_file) {
    int fd;
    size_t map_len;

    map_len = strlen(mapping);
    for (size_t j = 0; j < map_len; j++)
        if (mapping[j] == ',')
            mapping[j] = '\n';

    fd = open(map_file, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "ERROR: open %s: %s\n", map_file, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (write(fd, mapping, map_len) != map_len) {
        fprintf(stderr, "ERROR: write %s: %s\n", map_file, strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(fd);
}

static void proc_setgroups_write(pid_t child_pid, char *str) {
    char setgroups_path[PATH_MAX];
    int fd;

    snprintf(setgroups_path, PATH_MAX, "/proc/%jd/setgroups", (intmax_t) child_pid);

    fd = open(setgroups_path, O_RDWR);
    if (fd == -1) {
        if (errno != ENOENT)
            fprintf(stderr, "ERROR: open %s: %s\n", setgroups_path, strerror(errno));
        return;
    }

    if (write(fd, str, strlen(str)) == -1)
        fprintf(stderr, "ERROR: write %s: %s\n", setgroups_path, strerror(errno));

    close(fd);
}

int child_func(void *arg) {
    int *pipefd = (int *)arg;
    close(pipefd[1]);

    char buffer;
    if (read(pipefd[0], &buffer, 1) != 1) {
        perror("read");
        return 1;
    }
    close(pipefd[0]);

    char * namespace = "new_namespace";
    if (sethostname(namespace, strlen(namespace)) == -1)
    {
        perror("sethostname");
        return EXIT_FAILURE;
    }

    // if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
    //     perror("mount /proc");
    //     return EXIT_FAILURE;
    // }

    printf("Child process id = %d, user id = %d and group id = %d\n", getpid(), getuid(), getgid());

    execl("/bin/bash", "/bin/bash", (char *)NULL);

    perror("execl");

    return EXIT_FAILURE;
}

int main() {
    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return 1;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        return 1;
    }

    pid_t child_pid = clone(child_func, stack + STACK_SIZE, CLONE_NEWNS | CLONE_NEWPID | SIGCHLD, pipefd);
    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    printf("Parent process id = %d, user id = %d and group id = %d and child_pid = %d\n", getpid(), getuid(), getgid(), child_pid);

    char map_path[PATH_MAX];

    snprintf(map_path, PATH_MAX, "/proc/%ld/uid_map",  (intmax_t) child_pid);
    update_map("0 1000 1", map_path);
    printf("Updated UID map: %s\n", map_path);


    snprintf(map_path, PATH_MAX, "/proc/%ld/gid_map", (intmax_t) child_pid);
    update_map("0 1000 1", map_path);
    printf("Updated GID map: %s\n", map_path);
    
    proc_setgroups_write(child_pid, "deny");
    printf("Setgroups set to deny for: %s\n", map_path);

    close(pipefd[0]);

    if (write(pipefd[1], "1", 1) != 1) {
        perror("write");
        return 1;
    }
    close(pipefd[1]);

    if (waitpid(child_pid, NULL, 0) == -1) {
        perror("waitpid");
        return 1;
    }

    printf("Child process has terminated.\n");

    free(stack);
    return 0;
}
