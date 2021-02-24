#include "loguru/loguru.hpp"
#include "Messenger.h"
#include "util.h"

/**
 * This class implements a RAFT client application that provides a RAFT shell
 * (RASH) interface to the user, which forwards bash command strings to be run
 * by the servers in the RAFT cluster.
 */
class RaftClient {
  public:
    RaftClient(int client_port, const std::string cluster_file);
    std::string execute_command(std::string command);
    
  private:
    Messenger messenger;
    // { server number -> net address }
    unordered_map<int, std::string> server_addrs;
    int leader_no {1};  // best guess of current cluster leader
};
