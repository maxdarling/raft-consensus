/* includes */
#include "Networker.h"
#include "RaftRPC.pb.h"

/* All the information needed for one Messenger to talk to another */
struct serverInfo {
    struct sockaddr_in addr;
    int serverId;
}; 

const vector<serverInfo> SERVER_LIST {
    {}
};


/**
 * The Messenger class provides the abstraction of sending and receiving 
 * "messages" between servers.
 * 
 * Protocol buffers are used as the "message" format.  
 */

class Messenger {
    public: 
        Messenger(const int serverId, const vector<serverInfo>& serverList);
        ~Messenger();
        void sendMessage(const int serverId, const RPC::container& message);
        RPC::container getNextMessage();

    private:
        int _serverId; /* identifier for this server */
        Networker _networker; /* manage network-level communications */

        unordered_map<int, int> _serverIdToFd; /* bookkeep connections */

};
