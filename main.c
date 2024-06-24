#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define MAX_CMD_LEN 100
#define STACK_SIZE (1024 * 1024)

int run_command(void * args)
{
    char ** cmd_args = (char **)args;

    // Unshare the mount namespace to isolate it from the host
    if (unshare(CLONE_NEWNS) == -1) {
        perror("unshare");
        return EXIT_FAILURE;
    }

    char * namespace = "new_namespace";
    if (sethostname(namespace, strlen(namespace)) == -1)
    {
        perror("sethostname");
        return EXIT_FAILURE;
    }

    // Get the current working directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        perror("getcwd");
        return EXIT_FAILURE;
    }

    // Change root directory to the current working directory
    if (chroot(cwd) == -1)
    {
        perror("chroot");
        return EXIT_FAILURE;
    }

    // Change working directory to root
    if (chdir("/") == -1)
    {
        perror("chdir");
        return EXIT_FAILURE;
    }

    struct stat st = {0};
    if (stat("/proc", &st) == -1) {
        if (mkdir("/proc", 0755) == -1) {
            perror("mkdir /proc");
            return EXIT_FAILURE;
        }
    }

    // Mount /proc
    if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
        perror("mount /proc");
        return EXIT_FAILURE;
    }

    execvp(cmd_args[0], cmd_args);
    perror("execvp");

    // This code will only be reached if execvp fails
    // Unmount /proc before exiting
    if (umount("/proc") == -1) {
        perror("umount /proc");
    }
    if (rmdir("/proc") == -1) {
        perror("rmdir /proc");
    }

    return EXIT_FAILURE;
}

int main(int argc, char ** args)
{
    char * first = args[0];

    // char cmd[MAX_CMD_LEN] = "", **p;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s run <command> <args>\n", first);
        return EXIT_FAILURE;
    }

    char * second = args[1];

    if (strcmp(second, "run") != 0)
    {
        fprintf(stderr, "Unrecognized second argument.\nUsage: %s run <command> <args>\n", first);
        return EXIT_FAILURE;
    }

    // strcat(cmd, args[2]);
    
    // for(p = &args[3]; *p; p++)
    // {
    //     strcat(cmd, " ");
    //     strcat(cmd, *p);
    // }

    char * cmd_args[argc - 1]; // range is args[2] til args[argc - 1], size therefore is argc - 2, but we need 1 for the NULL terminator, so argc - 1
    for (int i = 2; i < argc; i++)
    {
        cmd_args[i - 2] = args[i];
    }
    cmd_args[argc - 2] = NULL;

    char * stack = malloc(STACK_SIZE);
    if (stack == NULL)
    {
        perror("malloc");
        return EXIT_FAILURE;
    }

    pid_t pid = clone(run_command, stack + STACK_SIZE, CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWPID | SIGCHLD, cmd_args);

    if (pid == -1) {
        perror("clone");
        free(stack);
        return EXIT_FAILURE;
    }


    // Wait for the child process to finish
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        free(stack);
        return EXIT_FAILURE;
    }

    // Unmount /proc and remove the directory after the child process finishes
    if (umount("proc") == -1) {
        perror("umount proc");
    }
    if (rmdir("proc") == -1) {
        perror("rmdir proc");
    }

    free(stack);

    // Return the exit status of the child process
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return EXIT_FAILURE;
    }
}