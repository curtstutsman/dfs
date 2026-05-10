// Server binary entry point: CLI argument parsing, signal handling, DFSServiceImpl setup.
#include <string>
#include <iostream>
#include <csignal>
#include <getopt.h>

#include "src/common/Utils.hpp"
#include "src/server/ServiceImpl.hpp"

void HandleSignal(int signum) {
    exit(0);
}

void Usage() {
    std::cout <<
        "\nUSAGE: dfs-server [OPTIONS]\n"
        "  -a, --address <address>          Bind address (default: 0.0.0.0:53552)\n"
        "  -d, --debug_level <0-3>          Verbosity: 0=errors only, 3=max (default: 0)\n"
        "  -m, --mount_path <path>          File storage root (default: mnt/server)\n"
        "  -n, --num_async_threads <num>    Async thread pool size (default: 4)\n"
        "  -h, --help                       Show this help\n\n";
    exit(1);
}

int main(int argc, char** argv) {
    const char* const short_opts = "a:d:m:n:h";
    const option long_opts[] = {
        {"address",           required_argument, nullptr, 'a'},
        {"debug_level",       required_argument, nullptr, 'd'},
        {"mount_path",        required_argument, nullptr, 'm'},
        {"num_async_threads", required_argument, nullptr, 'n'},
        {"help",              no_argument,       nullptr, 'h'},
        {nullptr,             no_argument,       nullptr,  0 }
    };

    int option_char;
    long num_async_threads = 4;
    std::string server_address = "0.0.0.0:53552";
    std::string mount_path = "mnt/server/";
    int debug_level = static_cast<int>(LL_ERROR);

    while ((option_char = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (option_char) {
            case 'a': server_address = std::string(optarg); break;
            case 'd': debug_level = std::stoi(optarg); break;
            case 'm': mount_path = std::string(optarg); break;
            case 'n': num_async_threads = std::stoi(optarg); break;
            default:  Usage(); break;
        }
    }

    if (debug_level > 0 && debug_level <= 3) {
        DFS_LOG_LEVEL = static_cast<dfs_log_level_e>(debug_level + 1);
    }

    signal(SIGINT,  HandleSignal);
    signal(SIGTERM, HandleSignal);

    DFSServiceImpl service(dfs_clean_path(mount_path), server_address, num_async_threads);
    service.Run();

    return 0;
}
