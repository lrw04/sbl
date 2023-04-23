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
int pipe_fd[2];
string cgroup;

int pivot_root(const char *new_root, const char *put_old) {
    return syscall(SYS_pivot_root, new_root, put_old);
}

int child_main(void *arg) {
    close(pipe_fd[1]);
    char ch;
    if (read(pipe_fd[0], &ch, 1)) ERREXIT("read");
    close(pipe_fd[0]);

    char **argv = (char **)arg;
    char *target = argv[1], *stdinf = argv[6], *stdoutf = argv[7],
         *stderrf = argv[8], *path = argv[9];
    int tl = atoi(argv[3]) + 500;
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
            exit((WIFEXITED(status) ? (WEXITSTATUS(status) ? 1 : 0) : 3));
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
        execve(path, argv + 9, envp);
        exit(EXIT_FAILURE);
    }
    return 0;
}

void sigint_handler(int sig) {
    kill(child_pid, SIGKILL);
    cout << "interrupted" << endl;
    rmdir(cgroup.c_str());
    exit(EXIT_FAILURE);
}

string get_key(string s, string k) {
    stringstream ss(s);
    string kk, vv;
    while (ss >> kk >> vv) {
        if (kk == k) return vv;
    }
    return "";
}

void run_main(int argc, char **argv) {
    if (argc < 10) {
        cerr << "Usage: run <target> <cgroup> <time> <memory> <pids> <stdin> "
                "<stdout> <stderr> <cmd>"
             << endl;
        exit(EXIT_FAILURE);
    }

    if (pipe(pipe_fd) == -1) ERREXIT("pipe");
    long long mem_limit = stoll(argv[4]) * 1024 * 1024;
    int pids_limit = stoi(argv[5]);
    const int flags = CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWPID |
                      CLONE_NEWUTS | SIGCHLD;
    cgroup = argv[2];
    cgroup += "/sbl.XXXXXX";
    if (!mkdtemp(cgroup.data())) ERREXIT("mkdtemp");
    write_file(cgroup + "/cpu.max", "100000");
    write_file(cgroup + "/memory.high", to_string(mem_limit));
    write_file(cgroup + "/memory.max", to_string(mem_limit));
    write_file(cgroup + "/pids.max", to_string(pids_limit));
    child_pid =
        clone(child_main, child_stack + sizeof child_stack, flags, argv);
    write_file(cgroup + "/cgroup.procs", to_string(child_pid));
    if (child_pid < 0) ERREXIT("clone");
    close(pipe_fd[1]);
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
    cout << "memory " << read_file(cgroup + "/memory.peak");
    cout << "cpu "
         << stoll(get_key(read_file(cgroup + "/cpu.stat"), "usage_usec")) / 1000
         << endl;
    rmdir(cgroup.c_str());
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
