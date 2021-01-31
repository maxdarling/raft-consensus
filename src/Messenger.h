#include "Networker.h"


/**
 * The Messenger class provides the abstraction of sending and receiving 
 * delimited messages between a pre-specified group of nodes.  
 * 
 * 
 * todo: add detail about shadow messages, clients
 */

class Messenger {
    public: 
        Messenger(const int serverId, const unordered_map<int, struct sockaddr_in>& serverList);
        ~Messenger();
        void sendMessage(const int serverId, std::string message);
        std::optional<std::string> getNextMessage();

    private:
        // todo: remove and solely define these functions in source file?
        void collectMessagesRoutine();
        void _sendMessage(const int serverId, std::string message, bool isShadow);


        /* identifier for this server (from server list) */
        int _serverId; 

        /* manage network-level communications */
        Networker* _networker; 

        /* synchronize shared accesses of the members below */
        std::mutex _m; 

        /* bookkeep connections */
        unordered_map<int, int> _serverIdToFd; 
        unordered_map<int, struct sockaddr_in> _serverIdToAddr;

        /* store collected messages */
        queue<std::string> _messageQueue;
};
