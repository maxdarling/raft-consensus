#include "Messenger.h"

class Client {
  public:
    Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo);
    void run();
    
  private:
    Messenger _messenger;
    unsigned int _addr;
    int _port;
    int _leaderID {1};

    std::string executeCommand(std::string command);
};
