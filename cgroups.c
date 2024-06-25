#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#define STACK_SIZE (1024 * 1024)  // Stack size for the child process
#define CGROUP_PATH "/sys/fs/cgroup/mycgroup"
#define CGROUP_PROCS_FILE "cgroup.procs"
#define CGROUP_MEMORY_FILE "memory.max"
#define CGROUP_CPU_FILE "cpu.max"

#define MEMORY_BLOCK_SIZE (1024 * 1024)  // 1 MB

void memory_stress_test() {
    size_t total_allocated = 0;
    while (1) {
        void *block = malloc(MEMORY_BLOCK_SIZE);
        if (block == NULL) {
            perror("Failed to allocate memory");
            break;
        }
        memset(block, 0, MEMORY_BLOCK_SIZE);
        total_allocated += MEMORY_BLOCK_SIZE;
        printf("Allocated %zu MB of memory\n", total_allocated / (1024 * 1024));
        // sleep(1);  // Delay to observe memory usage gradually
    }
    printf("Memory allocation stopped at %zu MB\n", total_allocated / (1024 * 1024));
}

void cpu_stress_test() {
    struct timeval start, end;
    long seconds, useconds, mseconds;

    gettimeofday(&start, NULL);

    while (1) {
        gettimeofday(&end, NULL);
        seconds  = end.tv_sec  - start.tv_sec;
        useconds = end.tv_usec - start.tv_usec;
        mseconds = ((seconds) * 1000 + useconds/1000.0) + 0.5;

        if (mseconds > 10000) {  // Run intense CPU usage for 10 seconds
            break;
        }
    }

    printf("CPU was intensely used for approximately %ld milliseconds\n", mseconds);
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
    fprintf(fp, value);
    fclose(fp);
    return 0;
}

int add_pid_to_cgroup(const char *cgroup_path, pid_t pid) {
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    return set_cgroup_value(cgroup_path, CGROUP_PROCS_FILE, pid_str);
}

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
    printf("Child process is running with limited resources.\n");

    // memory_stress_test();
    cpu_stress_test();
    printf("Allocated memory.\n");

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

    pid_t child_pid = clone(child_func, stack + STACK_SIZE, CLONE_NEWPID | SIGCHLD, pipefd);
    if (child_pid == -1) {
        perror("clone");
        return 1;
    }

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
    if (add_pid_to_cgroup(CGROUP_PATH, child_pid) != 0) {
        return 1;
    }

    // Parent process logic
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
    rmdir(CGROUP_PATH);  // Remove the cgroup

    return 0;
}
