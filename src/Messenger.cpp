#include "Messenger.h"

/* low-level networking */
#include <arpa/inet.h>
#include <condition_variable>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <errno.h>

/* general */
#include <unistd.h>
#include <cassert>
#include <thread>
#include <chrono>

using std::chrono::steady_clock;

/* loguru priority: logs to file, not to stderr. */
const int LOG_PRIORITY = 4;

/* convenience methods */
int createListeningSocket(int port);
int establishConnection(std::string peerAddr);
std::optional<std::string> readMessageFromSocket(int sockfd);
bool sendMessageOnSocket(int sockfd, const std::string& msg);

/** 
 * ~~~~~~ Design Notes ~~~~~~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message
 * itself.
 *
 * 
 * Race condition:
 * currently, there's a race when the destructor calls 'close()' on open
 * sockets. if a worker is about to call send/receive, then the destructor
 * calls close, then another program opens a file with the same fd, then the
 * workers will be reading/writing the wrong fd.
 * 
 * during program execution when a connection is closed, the workers both
 * detect that on their own by returning from send/receive, and the last of the
 * two workers to exit call 'close()', which prevents this issue.
 *
 * However, the destructor must call 'close()' as it is the only means of
 * waking up the workers from their blocking send/receive calls. However, once
 * this happens, the workers cannot validate that their socket is valid before
 * calling send/receive. If the worker could atomically check its fd and call
 * send/receive, then it'd be fine, but we'd need the worker to aquire a lock
 * and then release it once it calls send/receive (just like cv.wait
 * semantics), but that's impossible.
 *
 * ~~~~~~ Design Notes ~~~~~~
 */ 


/**
 * Creates a server instance with a listener on the given port.
 * 
 * Note: port should be in host-byte order. 
 */
Messenger::Messenger(const int listenPort) {
    // start a background thread to listen for connections
    _listenSock = createListeningSocket(listenPort);
    VLOG_F(LOG_PRIORITY, "listening socket is %d", *_listenSock);
    std::thread listener(&Messenger::listener, this);
    listener.detach();
}

/**
 * Creates a client instance. 
 * No background listener thread needed, so nothing to do.
 */
Messenger::Messenger() {}

/**
 * Messenger destructor. 
 */
Messenger::~Messenger() {
    // close listener thread so it can exit (only for server instances)
    if (_listenSock) {
        close(*_listenSock);
    }
    
    // close all open sockets. each socket's workers awaken and finish cleanup.
    std::lock_guard<std::mutex> lock(_m);
    for (auto it = _socketToState.begin(); it != _socketToState.end(); ++it) {
        close(it->first);
        it->second->destructorClosed = true;
    }
}


/**
 * Background thread routine to accept incoming connection requeuests.
 */
void Messenger::listener() {
    while(true) {
        int sockfd = accept(*_listenSock, nullptr, nullptr); 
        if (sockfd == -1) {
            VLOG_F(LOG_PRIORITY, "listener(): %s", strerror(errno));
            if (errno == EBADF) {
                VLOG_F(LOG_PRIORITY, "listener(): socket was closed, exiting");
                return;
            }
            VLOG_F(LOG_PRIORITY, "listener(): non-fatal error, continuing");
            continue;
        }
        VLOG_F(LOG_PRIORITY, "accepted connection on socket %d", sockfd);

        // create a new shared state variable for this socket 
        std::lock_guard<std::mutex> lock(_m);
        assert(!_socketToState.count(sockfd)); // guarantee prior cleanup
        _socketToState[sockfd] = new SocketState{}; 
        _socketToState[sockfd]->timeCreated = steady_clock::now(); 

        // start a request reciever for this socket's lifetime
        std::thread reader(&Messenger::receiveMessagesTask, this, sockfd, true);
        reader.detach();

        // create a reponse sender for this socket's lifetime
        std::thread sender(&Messenger::sendMessagesTask, this, sockfd);
        sender.detach();
    } 
}


/**
 * Receiver worker task. Continually reads messages and places them in the 
 * appropriate queue. 
 *
 * 'requestsNotResponses' - true if requests should be processed, false if 
 *                          reponses should be processed. 
 */
void Messenger::receiveMessagesTask(int sockfd, bool requestsNotResponses) {
    while(true) {
        std::optional<std::string> msgOpt = readMessageFromSocket(sockfd);
        if (!msgOpt) break;

        // todo: this is a bit ugly. a function pointer method might be 
        // superior. or passing pointers to the queues. 
        // additionally, perhaps two seperate funcs could be made (we'd want
        // to decompose the cleanup logic into a method in that case, though) 
        if (requestsNotResponses) { 
            Request request(*msgOpt, sockfd, steady_clock::now(), *this);
            _requestQueue.push(request);
        } else {
            _responseQueue.push(*msgOpt);
        }
    }

    // we broke from the loop due to an error. cleanup socket and exit. 
    std::lock_guard<std::mutex> lock(_m);
    if (_socketToState[sockfd]->oneExited) {
        if (!_socketToState[sockfd]->destructorClosed){
            close(sockfd); // if the destructor closed the socket, we don't. 
        }
        _peerAddrToSocket.erase(_socketToState[sockfd]->peerAddr);
        free(_socketToState[sockfd]);
        _socketToState.erase(sockfd);
        VLOG_F(LOG_PRIORITY, "receiver: cleaned up socket %d", sockfd);
    } else {
        _socketToState[sockfd]->oneExited = true;
        /* wake up the sender */
        _socketToState[sockfd]->outboundMessages.push("");
        VLOG_F(LOG_PRIORITY, 
               "receiver: will let sender cleanup socket %d", sockfd);
    }
}


/**
 * Worker task for 'sender' threads in which the thread will perpetually wait 
 * for messages to send to the given socket, exiting only once the socket dies.
 */
void Messenger::sendMessagesTask(int sockfd) {
    _m.lock();
    BlockingQueue<std::string>& outBoundMessages = 
        _socketToState[sockfd]->outboundMessages;
    _m.unlock(); /* todo: confirm that this is sufficient to avoid issues
                    when the map resizes and copies are performed. the map 
                    stores pointers, so I think it should be fine. */

    while(true) {
        std::string message = outBoundMessages.waitingPop();

        /* check if we were signaled by the receiver for this socket about a
           socket issue while we were waiting for a message to send */
        {
            std::lock_guard<std::mutex> lock(_m);
            if (_socketToState[sockfd]->oneExited) {
                break;
            }
        }

        if (!sendMessageOnSocket(sockfd, message)) {
            break;
        }
    }

    // we broke from the loop due to an error. cleanup socket and exit.
    std::lock_guard<std::mutex> lock(_m);
    if (_socketToState[sockfd]->oneExited) {
        if (!_socketToState[sockfd]->destructorClosed){
            close(sockfd); // if the destructor closed the socket, we don't. 
        }
        _peerAddrToSocket.erase(_socketToState[sockfd]->peerAddr);
        free(_socketToState[sockfd]);
        _socketToState.erase(sockfd);
        VLOG_F(LOG_PRIORITY, "sender: cleaned up socket %d", sockfd);
    } else {
        _socketToState[sockfd]->oneExited = true;
        VLOG_F(LOG_PRIORITY, 
               "sender: will let receiver cleanup socket %d", sockfd);
    }
}


/**
 * Send a request message to the designated address. The message is sent on a 
 * "best-effort" basis, as we cannot make guarantees about the network or peer.
 * 
 * 'peerAddr': "<a valid IPv4 dotted address>:<port>"
 *                example: "127.0.0.95:8000" 
 */
void Messenger::sendRequest(std::string peerAddr, std::string message) { 
    int sockfd;
    std::lock_guard<std::mutex> lock(_m);
    // make a connection if it's the first time sending to this address
    if (!_peerAddrToSocket.count(peerAddr)) {
        sockfd = establishConnection(peerAddr);
        if (sockfd == -1) {
            VLOG_F(LOG_PRIORITY, "connection failed to %s", peerAddr.c_str());
            return; 
        }
        VLOG_F(LOG_PRIORITY, "connection established with %s", peerAddr.c_str());
        _peerAddrToSocket[peerAddr] = sockfd;

        // create shared state for this socket
        assert(!_socketToState.count(sockfd)); // ensure proper earlier cleanup
        _socketToState[sockfd] = new SocketState{};
        _socketToState[sockfd]->peerAddr = peerAddr;

        // create a request sender for this socket's lifetime
        std::thread sender(&Messenger::sendMessagesTask, this, sockfd);
        sender.detach();

        // create a response receiver for this socket's lifetime
        std::thread reader(&Messenger::receiveMessagesTask, this, sockfd, false);
        reader.detach();
    }
    sockfd = _peerAddrToSocket[peerAddr];


    // pass the message to the socket's desigated sender 
    _socketToState[sockfd]->outboundMessages.push(message);
}


/**
 * Send a response to a peer's request. Sent on a "best-effort" basis. 
 *
 * Multiple responses can be sent using the same request object, as long as the
 * network connection hasn't been closed since the time of request receipt.
 */
void Messenger::Request::sendResponse(std::string message) {
    std::lock_guard<std::mutex> lock(_messengerParent._m);
    if (!_messengerParent._socketToState.count(_sockfd) || 
        _messengerParent._socketToState[_sockfd]->timeCreated > _timestamp) {
        VLOG_F(LOG_PRIORITY, "sendResponse: can't respond, connection to "
            "requester was closed");
        return;
    }

    // pass the message to the socket's desigated sender 
    _messengerParent.
        _socketToState[_sockfd]->outboundMessages.push(message);
}


/** 
 * Return a request message if one becomes available in the specified duration, 
 * in milliseconds. 
 *
 * A negative timeout indicates an indefinite wait duration.  
 */
std::optional<Messenger::Request> Messenger::getNextRequest(int timeoutMs) {
    if (timeoutMs < 0) {
        return _requestQueue.waitingPop();
    }
    return _requestQueue.waitingPop_timed(timeoutMs);
}


/** 
 * Return a response message if one becomes available in the specified duration,
 * in milliseconds. 
 *
 * A negative timeout indicates an indefinite wait duration. 
 */
std::optional<std::string> Messenger::getNextResponse(int timeoutMs) {
    if (timeoutMs < 0) {
        return _responseQueue.waitingPop();
    }
    return _responseQueue.waitingPop_timed(timeoutMs);
}


/**
 * Create a listening socket on the designated port (in host-byte order). 
 *
 * Returns the created socket. 
 *
 * A Messenger::Exception is thrown if the socket cannot be created. 
 */
int createListeningSocket(int port) {
    sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(port);

    int listenfd;
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        VLOG_F(LOG_PRIORITY, "%s", strerror(errno));
        throw Messenger::Exception("fatal error: 'socket()' failed");
    } 
    /* 'SO_REUSEADDR' prevents 'bind() failed: address already in use' 
        errors when restarting */
    int enable = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(int)) < 0) {
        VLOG_F(LOG_PRIORITY, "%s", strerror(errno));
        throw Messenger::Exception("fatal error: 'setsockopt()' failed");
    }
    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        VLOG_F(LOG_PRIORITY, "%s", strerror(errno));
        throw Messenger::Exception("fatal error: 'bind()' failed");
    }
    int MAX_BACKLOG_CONNECTIONS = 20;
    if (listen(listenfd, MAX_BACKLOG_CONNECTIONS) < 0) {
        VLOG_F(LOG_PRIORITY, "%s", strerror(errno)); 
        throw Messenger::Exception("fatal error: 'listen()' failed");
    }
    return listenfd;
}


/**
 * Create a socket connection to the designated peer address. 
 * The string input must be of the form "<IP>:<port>", where the port is in 
 * IPv4 decimal notation. A valid example input is: "127.0.0.95:8000". 
 *
 * Returns the socket fd on success, or -1 on failure. 
 */
int establishConnection(std::string peerAddr) {
    // parse input string 
    int colonIdx = peerAddr.find(":");
    std::string IPstr = peerAddr.substr(0, colonIdx);
    int port = std::stoi(peerAddr.substr(colonIdx + 1));

    sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; // use IPv4
    serv_addr.sin_port = htons(port);
    if (0 == inet_aton(IPstr.c_str(), &serv_addr.sin_addr)) {
        VLOG_F(LOG_PRIORITY, "bad address.");
        return -1;
    }

    // make the connection
    int connfd;
    if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        VLOG_F(LOG_PRIORITY, "Error: socket() failed");
        return -1;
    } 
    int enable = 1;
    /* 'SO_NOSIGPIPE': don't crash if we attempt to call 'send' on a closed 
        socket (this is equivalent to send's 'MSG_NOSIGNAL'). */
    if (setsockopt(connfd, SOL_SOCKET, SO_NOSIGPIPE,
                   &enable, sizeof(int)) < 0) {
        VLOG_F(LOG_PRIORITY, "%s", strerror(errno));
        throw Messenger::Exception("fatal error: 'setsockopt()' failed");
    }
    if(connect(connfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        VLOG_F(LOG_PRIORITY, "Error: connect() failed");
        return -1;
    } 

    return connfd; 
}


/** 
 * Send a length delimited message on the specified socket, blocking until all 
 * bytes have been sent. 
 * 
 * Returns true if the message was sent successfully, or false otherwise. 
 */
bool sendMessageOnSocket(int sockfd, const std::string& msg) {
    // send length
    int msgLength = msg.size();
    int n = send(sockfd, (char *)&msgLength, sizeof(msgLength), 0);
    if (n < 0 || n < sizeof(msgLength)) {
        return false;
    }

    // send message
    n = send(sockfd, msg.c_str(), msg.size(), 0);
    if (n < 0 || n < msg.size()) {
        return false;
    }
    return true;
}


/** 
 * Read the next length-delmited message on a socket, blocking until one is
 * ready.
 * 
 * Optionally returns the message, or a null option if there was an error. 
 */
std::optional<std::string> readMessageFromSocket(int sockfd) {
    // step 1: read length
    int msgLength;
    int n = recv(sockfd, (char *)&msgLength, sizeof(msgLength), MSG_WAITALL);
    if (n < 0 || n < sizeof(msgLength)) {
        return std::nullopt;
    }

    // step 2: read message
    char msgBuf [msgLength];
    n = recv(sockfd, msgBuf, sizeof(msgBuf), MSG_WAITALL);
    if (n < 0 || n < sizeof(msgBuf)) {
        return std::nullopt;
    }

    return std::string(msgBuf, msgLength);
}