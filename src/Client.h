#include "Messenger.h"
#include "util.h"

/**
 * This class implements a RAFT client application that provides a RAFT shell
 * (RASH) interface to the user, which forwards bash command strings to be run
 * by the servers in the RAFT cluster.
 */
class Client {
  public:
    Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo);
    void run();
    
  private:
    Messenger _messenger;
    unsigned int _clientAddr;
    int _clientPort;
    int _clusterSize;
    int _leaderID {1};  // Best guess as to whom the cluster leader is currently

    std::string executeCommand(std::string command);
};
