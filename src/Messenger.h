#include "Networker.h"
#include <unordered_set>

using std::unordered_set;


/**
 * The Messenger class implements message-based communication between 
 * a grid of pre-specified servers and optional clients. Messenger servers
 * automatically connect to one another at startup, and can easily re-join the
 * grid after crashing.
 * 
 * Usage Notes:
 * -Messenger servers will not finish initializing until they've connected to 
 * all other servers in the server list. This is designed to make coordination
 * easier for the client (eg. starting instances in many different terminal 
 * windows). As this is a rigid constraint, future revisions may add a timeout
 * window. Note, this rule does not apply to client Messenger instances.
 * 
 * -To re-join the grid after a crash, simply initialize the Messenger 
 * identically to before. There are no extra steps needed.  
 */
class Messenger {
    public: 
        /* server */
        Messenger(const int serverId, 
                  const unordered_map<int, sockaddr_in>& serverList);
        /* client */
        Messenger(const unordered_map<int, sockaddr_in>& serverList, 
                  const int clientPort);
         
        ~Messenger();
        
        bool sendMessageToServer(const int serverId, std::string message);
        bool sendMessageToClient(const sockaddr_in clientAddr, 
                                 std::string message);

        std::optional<std::string> getNextMessage();

    private:
        // should these be defined in here, too, or only in the source file?
        void collectMessagesRoutine();
        bool _sendMessage(const int serverId, std::string message, 
                          bool isShadow = false, bool isIntendedForClient = false, 
                          sockaddr_in clientAddr = {});

        /* distinguish server instances from client ones */
        bool _isClient;

        /* identifier for this server instance, from server list */
        int _serverId; 

        /* manage network-level communications */
        Networker* _networker; 

        /* synchronize shared accesses of the members below */
        std::mutex _m; 

        /* bookkeep connections */
        unordered_map<int, int> _serverIdToFd; 
        unordered_map<int, struct sockaddr_in> _serverIdToAddr;
        unordered_set<int> _closedConnections;
        map<sockaddr_in, int, 
            bool(*)(const sockaddr_in a, const sockaddr_in b)> _clientAddrToFd;

        /* store collected messages */
        queue<std::string> _messageQueue;
};
