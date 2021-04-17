#include "Messenger.h"
#include "util.h"

/**
 * This class implements a RAFT client which forwards bash command strings to 
 * be run by servers in the RAFT cluster specified by a server address file.
 * See the README for details.
 */
class RaftClient {
  public:
    RaftClient(const std::string cluster_file);
    std::string execute_command(std::string command);
    
  private:
    Messenger messenger;
    /* Maps { server number -> net address } */
    unordered_map<int, std::string> server_addrs;
    /* Best guess of the current cluster leader. */
    int leader_no {1};
};
