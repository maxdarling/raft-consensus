#include "Messenger.h"
#include "util.h"

/**
 * This class implements a RAFT client application that provides a RAFT shell
 * (RASH) interface to the user, which forwards bash command strings to be run
 * by the servers in the RAFT cluster.
 */
class Client {
  public:
    Client(std::string myHostAndPort, const unordered_map<int, std::string>& cluster_map);
    void run();
    
  private:
    Messenger _messenger;
    std::string _myHostAndPort;
    unordered_map<int, std::string> _cluster_map; // maps each cluster node's ID to it's network address
    int _leaderID {1};  // Best guess as to whom the cluster leader is currently

    std::string executeCommand(std::string command);
};
