#include "Networker.h"

#include <unordered_set>

using std::unordered_set;

// todo: figure out how to hide this in the source somehow
struct cmpByAddr {
    bool operator()(const struct sockaddr_in& a, const struct sockaddr_in& b) const {
        if (a.sin_port != b.sin_port) {
            return a.sin_port < b.sin_port;
        }
        return a.sin_addr.s_addr < b.sin_addr.s_addr;
    }
};

struct clientAddrAndFd {
    sockaddr_in clientAddr;
    int connfd;
};

/**
 * The Messenger class provides the abstraction of sending and receiving 
 * delimited messages between a pre-specified group of nodes.  
 * 
 * 
 * todo: add detail about shadow messages, clients
 */

class Messenger {
    public: 
        Messenger(const int serverId, const unordered_map<int, struct sockaddr_in>& serverList, 
                  bool isClient = false, int clientPort = -1);
        ~Messenger();
        
        void sendMessage(const int serverId, std::string message);
        void sendMessageToServer(const int serverId, std::string message);
        void sendMessageToClient(const sockaddr_in clientAddr, std::string message);

        std::optional<std::string> getNextMessage();

    private:
        // todo: remove and solely define these functions in source file?
        void collectMessagesRoutine();
        void _sendMessage(const int serverId, std::string message, bool isShadow, 
                          bool isIntendedForClient, int clientConnFd /*const sockaddr_in clientAddr*/);


        /* identifier for this server (from server list) */
        int _serverId; 

        /* used to separate client and server functionality */
        bool isClient;

        /* manage network-level communications */
        Networker* _networker; 

        /* synchronize shared accesses of the members below */
        std::mutex _m; 

        /* bookkeep connections */
        unordered_map<int, int> _serverIdToFd; 
        unordered_map<int, struct sockaddr_in> _serverIdToAddr;
        unordered_set<int> _closedConnections;

        //unordered_map<struct sockaddr_in, int, cmpByAddr> _clientAddrToFd;
        std::vector<clientAddrAndFd> _clientAddrAndFds;

        /* store collected messages */
        queue<std::string> _messageQueue;
};
