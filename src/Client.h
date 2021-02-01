#include "Messenger.h"
#include "Timer.h"

class Client {
  public:
    Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo);
    void run();
    
  private:
    Messenger _messenger;
    Timer _requestTimer;
    unsigned int _clientAddr;
    int _clientPort;
    int _clusterSize;
    int _leaderID {1};

    std::string executeCommand(std::string command);
};
