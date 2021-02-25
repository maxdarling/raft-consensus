#include "Client.h"
#include <iostream>

const int CLIENT_PORT = 3030;

void run_shell(RaftClient &c);

int main(int argc, char* argv[]) {
    loguru::init(argc, argv);
    loguru::add_file("client.log", loguru::Truncate, loguru::Verbosity_MAX);

    RaftClient c(CLIENT_PORT, DEFAULT_SERVER_FILE_PATH);
    run_shell(c);

    return 0;
}

/**
 * Launches a RAFT shell, which loops indefinitely, accepting commands to be
 * run on the RAFT cluster.
 */
void run_shell(RaftClient &c) {
    std::cout << "--- WELCOME TO RASH (THE RAFT SHELL) ---\n";
    for (;;) {
        std::string cmd;
        std::cout << "> ";
        std::getline(std::cin, cmd);
        std::cout << c.execute_command(cmd) << "\n";
    }
}
