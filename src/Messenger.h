#include "Networker.h"

#include <unordered_set>

using std::unordered_set;


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
                  bool isClient, int clientPort);
        ~Messenger();
        
        bool sendMessage(const int serverId, std::string message);
        bool sendMessageToServer(const int serverId, std::string message);
        bool sendMessageToClient(const sockaddr_in clientAddr, std::string message);

        std::optional<std::string> getNextMessage();

    private:
        // todo: remove and solely define these functions in source file?
        void collectMessagesRoutine();
        bool _sendMessage(const int serverId, std::string message, bool isShadow, 
                          bool isIntendedForClient, sockaddr_in clientAddr);


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
        map<sockaddr_in, int, bool(*)(const sockaddr_in a, const sockaddr_in b)> _clientAddrToFd;

        /* store collected messages */
        queue<std::string> _messageQueue;
};
