#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define MAX_CMD_LEN 100
#define STACK_SIZE (1024 * 1024)

int setup_temp_dir(char *temp_dir) {
    // Create a temporary directory in /tmp
    strcpy(temp_dir, "/tmp/mockerXXXXXX");
    if (!mkdtemp(temp_dir)) {
        perror("mkdtemp");
        return -1;
    }

    // Create necessary directories in the temporary directory
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/bin %s/proc", temp_dir, temp_dir);
    if (system(cmd) == -1) {
        perror("mkdir -p");
        return -1;
    }

    // Copy busybox to the temporary directory
    snprintf(cmd, sizeof(cmd), "cp /bin/busybox %s/bin/", temp_dir);
    if (system(cmd) == -1) {
        perror("cp busybox");
        return -1;
    }

    // Create symlinks for busybox applets
    snprintf(cmd, sizeof(cmd), "%s/bin/busybox --list > %s/applets.txt", temp_dir, temp_dir);
    if (system(cmd) == -1) {
        perror("busybox --list");
        return -1;
    }

    char applet_path[256];
    char applet_file[1024];
    snprintf(applet_file, sizeof(applet_file), "%s/applets.txt", temp_dir);
    FILE *applets_file = fopen(applet_file, "r");
    if (!applets_file) {
        perror("fopen applets.txt");
        return -1;
    }

    while (fgets(applet_path, sizeof(applet_path), applets_file)) {
        size_t len = strlen(applet_path);
        if (len > 0 && applet_path[len - 1] == '\n') {
            applet_path[len - 1] = '\0';
        }
        char symlink_path[1024];
        if (snprintf(symlink_path, sizeof(symlink_path), "%s/bin/%s", temp_dir, applet_path) >= sizeof(symlink_path)) {
            fprintf(stderr, "symlink path too long\n");
            fclose(applets_file);
            return -1;
        }
        if (symlink("/bin/busybox", symlink_path) == -1 && errno != EEXIST) {
            perror("symlink busybox");
            fclose(applets_file);
            return -1;
        }
    }

    fclose(applets_file);

    return 0;
}

int run_command(void * args)
{
    char ** cmd_args = (char **)args;
    char * temp_dir = cmd_args[0];
    char * command = cmd_args[1];

    // NOTE: Needed if calling clone with CLONE_NEWNS?
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

    // Change root directory to the temporary directory
    if (chroot(temp_dir) == -1)
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

    // Mount /proc
    if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
        perror("mount /proc");
        return EXIT_FAILURE;
    }

    execvp(command, &cmd_args[1]);
    perror("execvp");

    // This code will only be reached if execvp fails
    // Unmount /proc before exiting
    if (umount("/proc") == -1) {
        perror("umount /proc");
    }
    rmdir("/proc");

    return EXIT_FAILURE;
}

int main(int argc, char ** args)
{
    char * first = args[0];

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

    // Setup temporary directory
    char temp_dir[1024];
    if (setup_temp_dir(temp_dir) == -1) {
        return EXIT_FAILURE;
    }

    char ** cmd_args = malloc(sizeof(char *) * (argc - 1 + 1)); // range is args[2] til args[argc - 1], size therefore is argc - 2, but we need 1 for the NULL terminator, and 1 for temp_dir, so argc - 1 + 1
    if (!cmd_args) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    cmd_args[0] = temp_dir;
    for (int i = 2; i < argc; i++)
    {
        cmd_args[i - 1] = args[i];
    }
    cmd_args[argc - 1] = NULL;

    char * stack = malloc(STACK_SIZE);
    if (stack == NULL)
    {
        perror("malloc");
        free(cmd_args);
        return EXIT_FAILURE;
    }

    // Create a new UTS, mount, and PID namespace
    pid_t pid = clone(run_command, stack + STACK_SIZE, CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWPID | SIGCHLD, cmd_args);

    if (pid == -1) {
        perror("clone");
        free(stack);
        free(cmd_args);
        return EXIT_FAILURE;
    }

    // Wait for the child process to finish
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        free(stack);
        free(cmd_args);
        return EXIT_FAILURE;
    }

    // Unmount /proc and remove the directory after the child process finishes
    char proc_path[1024];
    if (snprintf(proc_path, sizeof(proc_path), "%s/proc", temp_dir) >= sizeof(proc_path)) {
        fprintf(stderr, "proc path too long\n");
        free(stack);
        free(cmd_args);
        return EXIT_FAILURE;
    }

    if (umount(proc_path) == -1) {
        perror("umount /proc");
    }

    // Remove temporary directory recursively
    char remove_cmd[1024];
    if (snprintf(remove_cmd, sizeof(remove_cmd), "rm -rf %s", temp_dir) >= sizeof(remove_cmd)) {
        fprintf(stderr, "remove command too long\n");
        free(stack);
        free(cmd_args);
        return EXIT_FAILURE;
    }

    if (system(remove_cmd) == -1) {
        perror("rm -rf temp_dir");
    }


    free(stack);
    free(cmd_args);

    // Return the exit status of the child process
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return EXIT_FAILURE;
    }
}
