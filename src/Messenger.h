//#include "Networker.h"
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include "BlockingQueue.h"

using std::unordered_map;
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

        std::optional<std::string> getNextMessage(int timeout = 0);

    private:
        int establishConnection(std::string hostAndPort);

        /* background thread routine to manage incoming connections */
        void listenerRoutine();
        void readMessagesTask(int sockfd);
        struct SenderState {
            BlockingQueue<std::string> outboundMessages;
            std::string hostAndPort;
        };
        unordered_map<int, SenderState*> _socketToSenderState;
        void sendMessagesTask(int sockfd);

        /* networking information for this instance */
        int _listenfd;
        int _myPort;

        /* maps addresses for healthy connections to the associated socket */
        unordered_map<std::string, int> _hostAndPortToFd;

        /* store collected messages */
        BlockingQueue<std::string> _messageQueue;

        std::condition_variable _cv;

        /* synchronize access to message queue for caller and background thread */
        std::mutex _m;
};
