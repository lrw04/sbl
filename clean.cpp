#include <sys/mount.h>
#include <unistd.h>
#include <iostream>
#include <string>
using namespace std;

// sbl-clean <target>
int main(int argc, char **argv) {
    if (argc != 2) {
        cerr << "Usage: sbl-clean <target>" << endl;
        return 1;
    }
    string s = argv[1];
    if (umount((s + "/tmp").c_str())) {
        perror("umount");
        return 1;
    }
    if (umount(s.c_str())) {
        perror("umount");
        return 1;
    }
    if (rmdir(s.c_str())) {
        perror("rmdir");
        return 1;
    }
}
