#include "dat_proc.hpp"

#include <cstdio>

#include <exception>
#include <fstream>
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << (argc > 0 ? argv[0] : "<executable>") << " <dat file> [backup path] [-fN (compression level)]" << std::endl;
        return 0;
    }
    std::string dat_name = argv[1];
    std::string bak_name = argc < 3 || argv[2][0] == '-' ? dat_name + ".BAK" : argv[2];

    if (argv[argc - 1][0] == '-' && argv[argc - 1][1] == 'f' &&
        argv[argc - 1][2] >= '0' && argv[argc - 1][2] <= '9')
        Zlib_Compression_Level = argv[argc - 1][2] - '0';

    try {
        std::cout << "Backing up original archive." << std::endl;
        int ret = std::rename(dat_name.c_str(), bak_name.c_str());
        if (ret != 0) throw std::ios_base::failure("Unable to move file. Does the backup file already exist?");

        std::ifstream in(bak_name, std::ios_base::in | std::ios_base::binary);
        std::ofstream out(dat_name, std::ios_base::out | std::ios_base::binary);
        std::cout << "Checking for textures that may require patching:" << std::endl;
        process_textures(in, out);
    } catch (std::exception &e) {
        std::cerr << "ERROR - " << e.what() << std::endl;
        throw;
    }

    std::cout << "Finished!" << std::endl;
    return 0;
}
