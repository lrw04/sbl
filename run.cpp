#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "util.h"

using namespace std;

const long long MEG = 1024 * 1024;

struct args {
    char **argv;
    string id;
};

int child(void *arg) {
    auto ar = *((args *)arg);
    char **argv = ar.argv;
    auto id = ar.id;
    filesystem::path target = argv[1];
    int time_limit = stoi(argv[2]), rt_limit = time_limit + 1000;
    long long memory_limit = stoll(argv[3]) * MEG;
    int pid_limit = stoi(argv[4]);
    long long stack_size = stoll(argv[5]) * MEG;
    filesystem::path report = argv[6];

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        perror("mount");
        return EXIT_FAILURE;
    }

    string cg_dir = "/sys/fs/cgroup";

    write_file(cg_dir + "/cpu/sbl." + id + "/tasks", "0");
    write_file(cg_dir + "/cpuacct/sbl." + id + "/tasks", "0");
    write_file(cg_dir + "/memory/sbl." + id + "/tasks", "0");
    write_file(cg_dir + "/pids/sbl." + id + "/tasks", "0");

    chdir(target.c_str());
    syscall(SYS_pivot_root, ".", ".");
    umount2(".", MNT_DETACH);

    setuid(65534);
    setgid(65534);

    execve(argv[7], argv + 7, nullptr);
    return 1;
}

// sbl-run <target> <time> <memory> <pid> <stack> <report> <cmd>
int main(int argc, char **argv) {
    if (argc < 8) {
        cerr << "Usage: sbl-run <target> <time> <memory> <pid> <stack> "
                "<report> <cmd>"
             << endl;
        return EXIT_FAILURE;
    }
    filesystem::path target = argv[1];
    int time_limit = stoi(argv[2]), rt_limit = time_limit + 1000;
    long long memory_limit = stoll(argv[3]) * MEG;
    int pid_limit = stoi(argv[4]);
    long long stack_size = stoll(argv[5]) * MEG;
    filesystem::path report = argv[6];

    const int flags = CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET |
                      CLONE_NEWUTS | SIGCHLD;
    void *stack = malloc(stack_size);
    if (!stack) {
        cerr << "Failed to allocate stack for child" << endl;
        return EXIT_FAILURE;
    }
    char *stack_top = (char *)stack + stack_size;

    char cgroup_dir[] = "/sys/fs/cgroup/cpu/sbl.XXXXXX";
    if (!mkdtemp(cgroup_dir)) {
        perror("mkdtemp");
        return EXIT_FAILURE;
    }
    string cg_dir = cgroup_dir;
    string id = cg_dir.substr(cg_dir.length() - 6, 6);
    args ar;
    ar.argv = argv;
    ar.id = id;
    cg_dir = "/sys/fs/cgroup";
    int ret = mkdir((cg_dir + "/cpuacct/sbl." + id).c_str(), 0700);
    if (ret && errno != EEXIST) {
        perror("mkdir");
        return EXIT_FAILURE;
    }
    ret = mkdir((cg_dir + "/memory/sbl." + id).c_str(), 0700);
    if (ret && errno != EEXIST) {
        perror("mkdir");
        return EXIT_FAILURE;
    }
    ret = mkdir((cg_dir + "/pids/sbl." + id).c_str(), 0700);
    if (ret && errno != EEXIST) {
        perror("mkdir");
        return EXIT_FAILURE;
    }
    write_file(cg_dir + "/cpu/sbl." + id + "/cpu.cfs_period_us", "100000");
    write_file(cg_dir + "/cpu/sbl." + id + "/cpu.cfs_quota_us", "100000");
    write_file(cg_dir + "/memory/sbl." + id + "/memory.limit_in_bytes",
               to_string(memory_limit));
    write_file(cg_dir + "/memory/sbl." + id + "/memory.memsw.limit_in_bytes",
               to_string(memory_limit));
    write_file(cg_dir + "/pids/sbl." + id + "/pids.max", to_string(pid_limit));

    pid_t child_pid = clone(child, stack_top, flags, &ar);
    if (child_pid < 0) {
        perror("clone");
        rmdir((cg_dir + "/cpu/sbl." + id).c_str());
        rmdir((cg_dir + "/cpuacct/sbl." + id).c_str());
        rmdir((cg_dir + "/memory/sbl." + id).c_str());
        rmdir((cg_dir + "/pids/sbl." + id).c_str());
        return EXIT_FAILURE;
    }
    int status;

    for (int t = 0; t < rt_limit; t += 100) {
        this_thread::sleep_for(chrono::milliseconds(100));
        waitpid(child_pid, &status, WNOHANG);
        if (WIFEXITED(status)) {
            ofstream st(report);
            st << (WIFEXITED(status) ? "ok" : "signalled") << endl;
            st << "exit "
               << (WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status))
               << endl;
            st << "cputime "
               << read_file(cg_dir + "/cpuacct/sbl." + id +
                            "/cpuacct.usage_user");
            st << "mem "
               << read_file(cg_dir + "/memory/sbl." + id +
                            "/memory.memsw.max_usage_in_bytes");
            st.close();

            rmdir((cg_dir + "/cpu/sbl." + id).c_str());
            rmdir((cg_dir + "/cpuacct/sbl." + id).c_str());
            rmdir((cg_dir + "/memory/sbl." + id).c_str());
            rmdir((cg_dir + "/pids/sbl." + id).c_str());
            return EXIT_SUCCESS;
        }
    }
    kill(child_pid, SIGKILL);
    ofstream st(report);
    st << "tle" << endl;
    st << "cputime "
       << read_file(cg_dir + "/cpuacct/sbl." + id + "/cpuacct.usage_user");
    st << "mem "
       << read_file(cg_dir + "/memory/sbl." + id +
                    "/memory.memsw.max_usage_in_bytes");
    st.close();
    rmdir((cg_dir + "/cpu/sbl." + id).c_str());
    rmdir((cg_dir + "/cpuacct/sbl." + id).c_str());
    rmdir((cg_dir + "/memory/sbl." + id).c_str());
    rmdir((cg_dir + "/pids/sbl." + id).c_str());
}
