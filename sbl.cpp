#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>

#include "util.h"

using namespace std;

void new_main(int argc, char **argv) {
    if (argc != 4) {
        cerr << "Usage: new <rootfs> <tmpfs-size> <target>" << endl;
        exit(EXIT_FAILURE);
    }
    string rootfs = argv[1], tmpfs_size = argv[2], target = argv[3];
    auto data = "mode=0777,size=" + tmpfs_size;
    if (mkdir(target.c_str(), 0777) && errno != EEXIST) ERREXIT("mkdir");
    if (mount(rootfs.c_str(), target.c_str(), "", MS_BIND, ""))
        ERREXIT("mount");
    if (mount("", target.c_str(), "", MS_REMOUNT | MS_RDONLY | MS_BIND, ""))
        ERREXIT("mount");
    if (mount("tmpfs", (target + "/tmp").c_str(), "tmpfs", 0, data.c_str()))
        ERREXIT("mount");
}

const int stack_size = 1024 * 1024;
static char child_stack[stack_size];

int child_pid;
string id;

int pivot_root(const char *new_root, const char *put_old) {
    return syscall(SYS_pivot_root, new_root, put_old);
}

int child_main(void *arg) {
    char **argv = (char **)arg;
    char *target = argv[1], *stdinf = argv[5], *stdoutf = argv[6],
         *stderrf = argv[7], *path = argv[8];
    int tl = atoi(argv[2]) + 500;
    if (sethostname("sandbox", 7) == -1) ERREXIT("sethostname");
    if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr))
        ERREXIT("mount");
    if (chdir(target)) ERREXIT("chdir");
    if (pivot_root(".", ".")) ERREXIT("pivot_root");
    if (umount2(".", MNT_DETACH)) ERREXIT("umount2");
    if (chdir("/tmp")) ERREXIT("chdir");
    if (mount("proc", "/proc", "proc", 0, nullptr)) ERREXIT("mount");
    freopen(stdinf, "r", stdin);
    freopen(stdoutf, "w", stdout);
    freopen(stderrf, "w", stderr);
    int pid = fork();
    if (pid) {
        for (int t = 0; t < tl; t += 100) {
            this_thread::sleep_for(chrono::milliseconds(100));
            int status;
            int ret = waitpid(pid, &status, WNOHANG);
            if (!ret) continue;
            if (ret == -1) ERREXIT("waitpid");
            exit((WIFEXITED(status) ? (WEXITSTATUS(status) ? 1 : 0)
                                    : (WIFSIGNALED(status) ? 3 : 0)));
        }
        exit(2);
    } else {
        char *envp[] = {nullptr};
        rlimit rl;
        rl.rlim_cur = rl.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_STACK, &rl);
        for (int fd = STDERR_FILENO + 1; fd < sysconf(_SC_OPEN_MAX); fd++)
            close(fd);
        setuid(65534);
        setgid(65534);
        execve(path, argv + 8, envp);
        exit(EXIT_FAILURE);
    }
    return 0;
}

void sigint_handler(int sig) {
    kill(child_pid, SIGKILL);
    cout << "interrupted" << endl;
    // TODO: cleanup cgroups
    exit(EXIT_FAILURE);
}

void run_main(int argc, char **argv) {
    if (argc < 9) {
        cerr << "Usage: run <target> <time> <memory> <pids> <stdin> <stdout> "
                "<stderr> <cmd>"
             << endl;
        exit(EXIT_FAILURE);
    }

    const int flags = CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWPID |
                      CLONE_NEWUTS | SIGCHLD;
    // TODO: prepare cgroups
    child_pid =
        clone(child_main, child_stack + sizeof child_stack, flags, argv);
    if (child_pid < 0) ERREXIT("clone");
    signal(SIGINT, sigint_handler);
    int status;
    waitpid(child_pid, &status, 0);
    if (WIFEXITED(status)) {
        int ret = WEXITSTATUS(status);
        if (ret == 0) {
            cout << "ok" << endl;
        } else if (ret == 1) {
            cout << "runtime-error" << endl;
        } else if (ret == 2) {
            cout << "time-limit-exceeded" << endl;
        } else if (ret == 3) {
            cout << "security-violation" << endl;
        }
    } else {
        cout << "unknown-error" << endl;
    }
    // TODO: retrieve resource usage
    // TODO: cleanup cgroups
}

void del_main(int argc, char **argv) {
    if (argc != 2) {
        cerr << "Usage: del <target>" << endl;
        exit(EXIT_FAILURE);
    }
    string target = argv[1];
    if (umount((target + "/tmp").c_str())) ERREXIT("umount");
    if (umount(target.c_str())) ERREXIT("umount");
    if (rmdir(target.c_str())) ERREXIT("rmdir");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        cerr << "Usage: sbl <new|run|del> <options>" << endl;
        return 1;
    }
    argc--;
    argv++;
    if (!strcmp(argv[0], "new")) {
        new_main(argc, argv);
    } else if (!strcmp(argv[0], "run")) {
        run_main(argc, argv);
    } else if (!strcmp(argv[0], "del")) {
        del_main(argc, argv);
    } else {
        cerr << "Unrecognized command" << endl;
        return 1;
    }
}
