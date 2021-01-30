/* includes */
#include "Networker.h"

/* Bundles information for inter-Messenger communication */
struct serverInfo {
    struct sockaddr_in addr;
    int serverId;
}; 

/**
 * The Messenger class provides the abstraction of sending and receiving 
 * delimited messages between a pre-specified group of nodes.  
 * 
 * Protocol buffers are used as the "message" format. (todo: generalize to str)
 */

class Messenger {
    public: 
        Messenger(const int serverId, const unordered_map<int, struct sockaddr_in>& serverList);
        ~Messenger();
        void sendMessage(const int serverId, std::string message);
        std::optional<std::string> getNextMessage();

    private:
        void collectMessagesRoutine();

        int _serverId; /* identifier for this server */
        Networker* _networker; /* manage network-level communications */

        std::mutex _m;

        unordered_map<int, int> _serverIdToFd; /* bookkeep connections */
        unordered_map<int, struct sockaddr_in> _serverIdToAddr;

        queue<std::string> _messageQueue; /* store collected messages */
};
