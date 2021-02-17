//#include "Networker.h"
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include "BlockingQueue.h"

using std::unordered_map;


/**
 * The Messenger class implements message-based communication between servers
 * and clients.  
 * 
 * Clients: send messages to servers and get responses. 
 * Servers: respond to clients, and send + receive with other servers. 
 */
class Messenger {
    public: 
        Messenger(int myPort); // server instance w/ listener 
        Messenger();           // client instance w/o listener
        ~Messenger();

        struct Request {
            std::string message;
            struct ResponseToken {
                int fd;
                std::chrono::time_point<std::chrono::system_clock> timestamp;
            } responseToken;
        };
        
        /* send a message to the peer at "127.0.0.95:8000", for example */
        bool sendRequest(std::string hostAndPort, std::string message); 
        std::optional<Request> getNextRequest(int timeout = 0);

        // todo: make getNextRequest return a request
        bool sendResponse(Request::ResponseToken responseToken, std::string message);
        std::optional<std::string> getNextResponse(int timeout); // todo: better timeout type? ms? 
        std::optional<std::string> awaitResponseFrom(std::string hostAndPort, int timeout);

    private:
        /* background thread routine to manage incoming connections */
        void listenerRoutine();
        void receiveMessagesTask(int sockfd, BlockingQueue<std::string>* readyMessages); // todo: get this to work. thread errors. 

        struct SenderState {
            BlockingQueue<std::string> outboundMessages;
            std::string hostAndPort; // only used for request senders to remove self from map. awkward??
        };
        unordered_map<int, SenderState*> _socketToSenderState;
        void sendMessagesTask(int sockfd);


        // todo: merge this into one. getting issues passing blocking queue to thread. 
        void receiveRequestsTask(int sockfd); 
        void receiveResponsesTask(int sockfd); 

        /* responses */
        unordered_map<int, std::chrono::time_point<std::chrono::system_clock>> _socketToTimeCreated;


        /* networking information for this instance */
        int _listenfd;
        int _myPort;

        /* maps addresses for healthy connections to the associated socket */
        unordered_map<std::string, int> _hostAndPortToFd;

        /* store collected messages */
        BlockingQueue<Request> _requestQueue;
        BlockingQueue<std::string> _responseQueue; 

        std::condition_variable _cv;

        /* synchronize access to message queue for caller and background thread */
        std::mutex _m;
};
