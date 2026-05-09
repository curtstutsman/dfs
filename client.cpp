// Client binary entry point: CLI argument parsing, signal handling, DFSClient setup.
#include <map>
#include <string>
#include <csignal>
#include <iostream>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

#include "src/common/Log.h"
#include "src/client/Client.h"

DFSClient client;

void HandleSignal(int signum) {
    client.Unmount();
    exit(0);
}

void Usage() {
    std::cout <<
        "\nUSAGE: dfs-client [OPTIONS] COMMAND [FILENAME]\n"
        "  -a, --address <address>          Server address (default: 0.0.0.0:53552)\n"
        "  -d, --debug_level <0-3>          Verbosity: 0=errors only, 3=max (default: 0)\n"
        "  -m, --mount_path <path>          Local mount directory\n"
        "  -t, --deadline_timeout <ms>      RPC deadline in milliseconds (default: 10000)\n"
        "  -h, --help                       Show this help\n"
        "\n"
        "COMMAND: mount | fetch | store | delete | list | stat\n"
        "FILENAME: required for fetch, store, delete, stat\n\n";
    exit(1);
}

int main(int argc, char** argv) {
    const char* const short_opts = "a:d:m:t:h";
    const option long_opts[] = {
        {"address",          optional_argument, nullptr, 'a'},
        {"debug_level",      optional_argument, nullptr, 'd'},
        {"mount_path",       optional_argument, nullptr, 'm'},
        {"deadline_timeout", optional_argument, nullptr, 't'},
        {"help",             no_argument,       nullptr, 'h'},
        {nullptr,            no_argument,       nullptr,  0 }
    };

    int option_char;
    std::string command;
    std::string mount_path;
    int debug_level = static_cast<int>(LL_ERROR);
    std::string filename;
    std::string server_address = "0.0.0.0:53552";
    int deadline_timeout = 10000;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        perror("getcwd");
        return 1;
    }
    std::string working_directory(cwd);

    while ((option_char = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (option_char) {
            case 'a': server_address = std::string(optarg); break;
            case 'd': debug_level = std::stoi(optarg); break;
            case 'm': mount_path = std::string(optarg); break;
            case 't': deadline_timeout = std::stoi(optarg); break;
            case 'h': Usage(); break;
            default:  Usage(); break;
        }
    }

    if (debug_level > 0 && debug_level <= 3) {
        DFS_LOG_LEVEL = static_cast<dfs_log_level_e>(debug_level + 1);
    }

    if (mount_path.empty()) {
        mount_path = working_directory + "/mnt/client";
    }

    struct stat st;
    if (stat(mount_path.c_str(), &st) != 0) {
        std::cerr << "Mount path not found: " << mount_path << std::endl;
        return 1;
    }

    for (int i = optind; i < argc; i++) {
        if (command.empty())        command = argv[i];
        else if (filename.empty())  filename = argv[i];
    }

    if (command.empty()) {
        std::cerr << "Missing command\n";
        Usage();
    }

    std::string nonpath_commands = "list mount";
    if (filename.empty() && nonpath_commands.find(command) == std::string::npos) {
        std::cerr << "Missing filename\n";
        Usage();
    }

    signal(SIGINT,  HandleSignal);
    signal(SIGTERM, HandleSignal);

    client.SetMountPath(mount_path);
    client.SetDeadlineTimeout(deadline_timeout);
    client.InitializeClientNode(server_address);
    client.ProcessCommand(command, filename);

    return 0;
}
