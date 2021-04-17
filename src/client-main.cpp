#include "Client.h"
#include <iostream>

void run_shell(RaftClient &c);

int main(int argc, char* argv[]) {
    // run the raft client application 
    try {
        RaftClient c(DEFAULT_SERVER_FILE_PATH);
        run_shell(c);
    }
    catch (Messenger::Exception& me) {
        std::cout << me.what() << std::endl;
    }
    catch (...) {
        std::cout << "General exception: fatal error" << std::endl;
    }

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
