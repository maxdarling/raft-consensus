#include "StateMachines/ShellCommandStateMachine.h"
#include "StateMachines/KVStateMachine.h"
#include "Server.h"
#include <iostream>

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    loguru::init(argc, argv);
    loguru::add_file("server.log", loguru::Truncate, loguru::Verbosity_MAX);

    if (argc < 2) {
        std::cerr << "Please specify the server number of this instance (i.e. "
            "which line of the server address list file corresponds to the "
            "instance you wish to launch?). See README for details.\n";
        return EXIT_FAILURE;
    }

    int serverNumber;
    try { serverNumber = std::stoi(argv[1]); }
    catch (const std::exception &exc) {
        std::cerr << "Invalid server number: " << exc.what();
        return EXIT_FAILURE;
    }

    bool restarting = false;
    if (argc >= 3) restarting = strcmp(argv[2], "-r") == 0 || 
                                strcmp(argv[2], "-R") == 0;

    
    //ShellCmdStateMachine sm;
    KVStateMachine sm;

    // run the raft server
    try {
        Server s(serverNumber, DEFAULT_SERVER_FILE_PATH, &sm, restarting);
        s.run();
    }
    catch (Messenger::Exception& me) {
        std::cout << me.what() << std::endl;
    }
    catch (PersistentStorage::Exception& pse) {
        std::cout << pse.what() << std::endl;
    }
    catch (...) {
        std::cout << "General exception: fatal error" << std::endl;
    }

    return 0;
}
