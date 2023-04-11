#pragma once

#include <fstream>
#include <string>
#include <sstream>

void write_file(std::string fn, std::string content) {
    std::ofstream st(fn);
    st << content;
    st.close();
}

std::string read_file(std::string fn) {
    std::ifstream st(fn);
    std::stringstream ss;
    ss << st.rdbuf();
    return ss.str();
}
