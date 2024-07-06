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
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>

#define MAX_CMD_LEN 100
#define STACK_SIZE (1024 * 1024)

#define CGROUP_PATH "/sys/fs/cgroup/mycgroup"
#define CGROUP_PROCS_FILE "cgroup.procs"
#define CGROUP_MEMORY_FILE "memory.max"
#define CGROUP_CPU_FILE "cpu.max"


typedef struct child_args {
    char ** cmd_args;
    int * pipefd; 
} child_args;

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

    // Copy cputest and memtest into temp folder (used to test cgroups)
    snprintf(cmd, sizeof(cmd), "cp cputest %s && chmod +x cputest", temp_dir);
    if (system(cmd) == -1) {
        perror("cp cputest");
        return -1;
    }
    snprintf(cmd, sizeof(cmd), "cp memtest %s && chmod +x memtest", temp_dir);
    if (system(cmd) == -1) {
        perror("cp memtest");
        return -1;
    }

    return 0;
}

int create_cgroup(const char *cgroup_path) {
    if (mkdir(cgroup_path, 0755) != 0) {
        if (errno != EEXIST) {
            perror("mkdir");
            return -1;
        }
    }
    fprintf(stdout, "created c group\n");
    return 0;
}

int set_cgroup_value(const char *cgroup_path, const char *file, const char *value) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", cgroup_path, file);
    FILE * fp = fopen(filepath, "wb");
    if (!fp)
    {
        perror("fopen");
        return -1;
    }
    fprintf(fp, "%s", value);
    fclose(fp);
    return 0;
}

int add_pid_to_cgroup(const char *cgroup_path, pid_t pid) {
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    return set_cgroup_value(cgroup_path, CGROUP_PROCS_FILE, pid_str);
}

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

int run_command(void * args)
{
    child_args * ca = (child_args *) args;
    int * pipefd = ca->pipefd;
    char ** cmd_args = ca->cmd_args;
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


    close(pipefd[1]);

    char buffer;
    if (read(pipefd[0], &buffer, 1) != 1) {
        perror("read");
        return 1;
    }
    close(pipefd[0]);
    
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

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        return 1;
    }

    child_args * ca = (child_args *)malloc(sizeof(child_args));
    ca->cmd_args = cmd_args;
    ca->pipefd = pipefd;


    // Create a new UTS, mount, and PID namespace
    pid_t pid = clone(run_command, stack + STACK_SIZE, CLONE_NEWUSER | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWNS | CLONE_NEWPID | SIGCHLD, ca);

    if (pid == -1) {
        perror("clone");
        free(stack);
        free(cmd_args);
        return EXIT_FAILURE;
    }


    char map_path[PATH_MAX];

    snprintf(map_path, PATH_MAX, "/proc/%ld/uid_map",  (intmax_t) pid);
    update_map("0 1000 1", map_path);
    printf("Updated UID map: %s\n", map_path);


    snprintf(map_path, PATH_MAX, "/proc/%ld/gid_map", (intmax_t) pid);
    proc_setgroups_write(pid, "deny");
    printf("Setgroups set to deny for: %s\n", map_path);
    update_map("0 1000 1", map_path);
    printf("Updated GID map: %s\n", map_path);

    // Create and configure cgroup
    if (create_cgroup(CGROUP_PATH) != 0) {
        return 1;
    }
    if (set_cgroup_value(CGROUP_PATH, CGROUP_MEMORY_FILE, "10000000") != 0) {  // 10 MB
        return 1;
    }
    if (set_cgroup_value(CGROUP_PATH, CGROUP_CPU_FILE, "10000 100000") != 0) {  // 10% of a CPU
        return 1;
    }
    if (add_pid_to_cgroup(CGROUP_PATH, pid) != 0) {
        return 1;
    }

    close(pipefd[0]);

    if (write(pipefd[1], "1", 1) != 1) {
        perror("write");
        return 1;
    }
    close(pipefd[1]);

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
