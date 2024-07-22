# mocker

## A docker lite clone

### compile

```
clang main.c -o mocker -Wall -O3
```


### run

```
sudo ./mocker run sh
```


### usage
```
mocker run <command> <arguments>
```

### example
```
$ mocker run echo "hello world!"
$ hello world!
```

### How does process isolation work?

**Namespace Isolation:** The mocker process is cloned using the clone system call with flags CLONE_NEWUTS, CLONE_NEWNS, and CLONE_NEWPID. This creates new UTS (hostname and NIS domain name), mount, and PID namespaces for the child process, ensuring it operates in a separate environment from the host.

**Hostname Setting:** The child process sets a new hostname using sethostname, helping to distinguish it from other processes and further enforcing isolation within its UTS namespace.

**Temporary Directory Setup:** The program creates a temporary directory in /tmp to act as the root filesystem for the isolated process. This directory contains minimal necessary subdirectories (/bin and /proc).

**Chroot Jail:** The child process changes its root directory to the temporary directory using chroot, effectively jailing the process so it cannot access files outside this directory.

**Copying Busybox:** The busybox binary is copied to the /bin directory within the temporary root. Busybox provides essential Unix utilities needed for the isolated environment.

**Mounting /proc:** The /proc filesystem is mounted inside the new root, allowing the isolated process to interact with its own set of processes and system information.

**Command Execution:** Finally, the specified command is executed within this isolated environment using execvp. The child process runs entirely within the confined namespace and directory structure set up earlier.

**Rootless:** UID and GID mapping are made betweeen a non root user in the parent process to the actual process run, effectively making it seem like the process is running as root from its viewpoint, whereas on the system its running as a non priviledged user.