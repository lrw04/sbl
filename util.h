#pragma once

#include <fstream>
#include <sstream>
#include <string>

#define ERREXIT(msg)        \
    do {                    \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (false)

void write_file(std::string fn, std::string content) {
    std::ofstream st(fn);
    if (!st) ERREXIT("write");
    st << content;
    st.close();
}

std::string read_file(std::string fn) {
    std::ifstream st(fn);
    if (!st) ERREXIT("read");
    std::stringstream ss;
    ss << st.rdbuf();
    return ss.str();
}
