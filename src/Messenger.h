//#include "Networker.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>

using std::unordered_map;
using std::vector;
using std::queue; 


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
        // store all necessary state for a socket manager worker thread
        struct SocketManagerState {
            int sockfd;
            queue<std::string> outboundMessages;
        };
        // thread routine for socket manager to execute
        void sendAndReceiveMessagesTask(SocketManagerState* state);
        unordered_map<int, SocketManagerState> _socketTable;


        void collectMessagesRoutine();
    
        int establishConnection(std::string hostAndPort);

        /* NETWORKER FIELDS */
        /* background thread routine to manage incoming connections */
        void listenerRoutine();

        /* networking information for this instance */
        int _listenfd;
        int _myPort;

        /* table of polled file descriptors */
        vector<struct pollfd> _pfds;

        /* synchronize accesses of the polling table and fd queue */
        //std::mutex _m;

        /* END NETWORKER FIELDS */

        /* maps addresses for healthy connections to the associated socket */
        unordered_map<std::string, int> _hostAndPortToFd;

        /* store collected messages */
        queue<std::string> _messageQueue;

        /* synchronize access to message queue for caller and background thread */
        std::mutex _m;
};
