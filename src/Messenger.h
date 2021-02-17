#include <unordered_map>
#include <mutex>
#include <chrono>
#include "BlockingQueue.h"

using std::unordered_map;
using std::chrono::steady_clock;


/**
 * The Messenger class implements message-based communication between servers
 * and clients.  
 * 
 * Clients: send messages to servers and get responses. 
 * Servers: respond to clients, and send/receive amongst other servers. 
 */
class Messenger {
    public: 
        Messenger(int myPort); // server instance w/ listener 
        Messenger();           // client instance w/o listener
        ~Messenger();

        struct Request {
            std::string message;
            struct ResponseToken {
                int sockfd;
                std::chrono::time_point<steady_clock> timestamp;
            } responseToken;
        };
        
        /* send a message to the peer at "127.0.0.95:8000", for example */
        bool sendRequest(std::string hostAndPort, std::string message); 
        std::optional<Request> getNextRequest(int timeout = 0);

        bool sendResponse(Request::ResponseToken responseToken, std::string message);
        std::optional<std::string> getNextResponse(int timeout); // todo: better timeout type? ms? 
        std::optional<std::string> awaitResponseFrom(std::string hostAndPort, int timeout);

    private:
        /* background thread routine to manage incoming connections */
        void listenerRoutine(int listenfd);

        //void receiveMessagesTask(int sockfd, BlockingQueue<std::string>* readyMessages); // todo: get this to work. thread errors. 
        void receiveMessagesTask(int sockfd, bool isRequestReceiver);
        // todo: merge this into one. getting issues passing blocking queue to thread. 
        // void receiveRequestsTask(int sockfd); 
        // void receiveResponsesTask(int sockfd); 

        void sendMessagesTask(int sockfd);

        /* shared state between a socket's sender and the main dispatching thread */
        struct SocketState {
            BlockingQueue<std::string> outboundMessages; // todo: make this a ptr. solve need to make SocketState a ptr? 
            std::string hostAndPort; // only used for request senders to remove self from map.

            // only used for response senders to check if sending is vaild 
            std::chrono::time_point<steady_clock> timeCreated;

            /* used to coordinate which of the reader / sender should close the socket, since 
               it's only safe to close ths socket once both threads have exited, and won't be 
               touching the socket anymore. 
            */
            bool oneExited = false;
        };
        /* maps socket to the state shared between its sender and the main dispatching thread */
        unordered_map<int, SocketState *> _socketToState;

        /* maps addresses for healthy connections to the associated socket */
        unordered_map<std::string, int> _hostAndPortToFd;

        /* store collected messages */
        BlockingQueue<Request> _requestQueue;
        BlockingQueue<std::string> _responseQueue; 

        std::mutex _m;
};
