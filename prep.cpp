#include <sys/mount.h>
#include <sys/stat.h>

#include <filesystem>
#include <iostream>
using namespace std;

// sbl-prep <rootfs> <tmpfs-size> <target>
int main(int argc, char **argv) {
    if (argc != 4) {
        cerr << "Usage: sbl-prep <rootfs> <tmpfs-size> <target>" << endl;
        return 1;
    }
    filesystem::path rootfs = argv[1], target = argv[3];
    string data = "mode=666,size=";
    data += argv[2];

    mkdir(target.c_str(), 0700);

    if (mount(rootfs.c_str(), target.c_str(), "", MS_BIND, "")) {
        perror("mount");
        return EXIT_FAILURE;
    }
    if (mount("", target.c_str(), "", MS_REMOUNT | MS_BIND | MS_RDONLY, "")) {
        perror("mount");
        return EXIT_FAILURE;
    }
    if (mount("tmpfs", (target / "tmp").c_str(), "tmpfs", 0, data.c_str())) {
        perror("mount");
        return EXIT_FAILURE;
    }
}
