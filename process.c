#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <dirent.h>
#include <sys/mount.h>

#define STACK_SIZE (1024 * 1024)  // Stack size for the child process

int child_func(void *arg) {
    int *pipefd = (int *)arg;
    close(pipefd[1]);  // Close the write end of the pipe

    // Wait for the parent process to write to the pipe
    char buffer;
    if (read(pipefd[0], &buffer, 1) != 1) {
        perror("read");
        return 1;
    }
    close(pipefd[0]);

    // Child process logic
    printf("Child process is running.\n");

    // Mount /proc
    if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
        perror("mount /proc");
        return EXIT_FAILURE;
    }

    printf("Child process id = %d and group id = %d\n", getpid(), getgid());


    DIR *d;
    struct dirent *dir;
    d = opendir("/proc");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
        printf("%s\n", dir->d_name);
        }
        closedir(d);
    }

    sleep(5); // check on host if host processes are visible with ps -ef

    // Unmount /proc before exiting
    if (umount("/proc") == -1) {
        perror("umount /proc");
    }
    rmdir("/proc");


    return 0;
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

    pid_t child_pid = clone(child_func, stack + STACK_SIZE, CLONE_NEWNS | CLONE_NEWUSER | CLONE_NEWPID | SIGCHLD, pipefd);
    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

    // Parent process logic

    printf("Parent process id = %d and group id = %d\n", getpid(), getgid());

    close(pipefd[0]);  // Close the read end of the pipe

    // Signal the child process to start
    if (write(pipefd[1], "1", 1) != 1) {
        perror("write");
        return 1;
    }
    close(pipefd[1]);

    // Wait for the child to terminate
    if (waitpid(child_pid, NULL, 0) == -1) {
        perror("waitpid");
        return 1;
    }

    printf("Child process has terminated.\n");

    // Clean up
    free(stack);

    return 0;
}
