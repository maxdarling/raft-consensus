#include "Messenger.h"

class Client {
  public:
    Client(const sockaddr_in &clientAddr, const unordered_map<int, sockaddr_in>& clusterInfo);
    std::string executeCommand(std::string command);

  private:
    Messenger _messenger;
};
