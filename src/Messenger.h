#include "Networker.h"
#include <unordered_set>

using std::unordered_set;


/**
 * The Messenger class implements message-based communication between servers
 * and clients.  
 * 
 * Clients: send messages to servers and get responses. 
 * Servers: respond to clients, and send + receive with other servers. 
 */
class Messenger {
    public: 
        Messenger(int myPort); // todo: add a client version so we only listen on server instances.  
        ~Messenger();
        
        /* send a message to the peer at "127.0.0.95:8000", for example */
        bool sendMessage(std::string hostAndPort, std::string message); 

        std::optional<std::string> getNextMessage();

    private:
        void collectMessagesRoutine();
    
        int establishConnection(std::string hostAndPort);

        /* manage network-level communications */
        Networker* _networker; 


        /* maps addresses for healthy connections to the associated socket */
        unordered_map<std::string, int> _hostAndPortToFd;

        /* store collected messages */
        queue<std::string> _messageQueue;

        /* synchronize access to message queue for caller and background thread */
        std::mutex _m;
};
