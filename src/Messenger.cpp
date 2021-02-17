#include "Messenger.h"

/* low-level networking */
#include <arpa/inet.h>
#include <condition_variable>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h> 

/* general */
#include <unistd.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using std::cout, std::endl;
using std::chrono::steady_clock;

/* convenience methods */
int createListeningSocket(int port);
int establishConnection(std::string hostAndPort);
int sendEntireMessage(const int connfd, const void* buf, const int length);
int readEntireMessage(const int connfd, void* buf, int bytesToRead);

/** 
 * ~~~~~~ Design Notes ~~~~~~
 * 
 * Network message delimiting: 
 * A simple protocol is used for delimiting messages. Each communication leads
 * with 4 bytes - denoting message length - and follows with the message
 * itself.
 *
 * ~~~~~~ Design Notes ~~~~~~
 */ 


/**
 * Messenger constructor for server instances. Setups up a listener on the given port.  
 * 
 * Note: port should be in host-byte order. 
 */
Messenger::Messenger(const int myPort) {
    // create a listening socket
    int listenfd = createListeningSocket(myPort);

    // start a background thread to listen for connections
    std::thread listener(&Messenger::listenerRoutine, this, listenfd);
    listener.detach();

    cout << "finished messenger constructor "<< endl;
}

/**
 * Messenger constructor for client instances. 
 */
Messenger::Messenger() {
    // start a networker on our assigned port
    cout << "finished client messenger constructor "<< endl;
}

/**
 * Class destructor. 
 */
Messenger::~Messenger() {
}


/**
 * Background thread routine to accept incoming connection requeuests.
 */
void Messenger::listenerRoutine(int listenfd) {
    //initialize members

    // Start actual listener routine
    while(true) {
        int sockfd = accept(listenfd, nullptr, nullptr); 
        if (sockfd == -1) {
            perror("accept failed\n");
        }

        // create a new shared state variable for this socket 
        if (_socketToState.count(sockfd)) {
            free(_socketToState[sockfd]);
        }
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
 * \param isRequestReceiver - determines whether to place messages into request
 *                            response queues.  
 *
 * Todo: try templatizing to allow the right type of blocking queue to be passed in
 */
void Messenger::receiveMessagesTask(int sockfd, bool isRequestReceiver) {
    while(true) {
        // read the message length
        int msgLength;
        int n = readEntireMessage(sockfd, &msgLength, sizeof(msgLength));
        if (n != sizeof(msgLength)) break;

        // read the message itself
        char msgBuf [msgLength];
        n = readEntireMessage(sockfd, msgBuf, sizeof(msgBuf));
        if (n != msgLength) break;

        // place the message in the appropriate queue
        std::string message(msgBuf, sizeof(msgBuf));
        if (isRequestReceiver) {
            Request request {message, sockfd, steady_clock::now()};
            _requestQueue.blockingPush(request);
        } else {
            _responseQueue.blockingPush(message);
        }
        cout << "Got a message of length " << msgLength << endl;
    }

    // we only break out of the loop if there was an error
    cout << "receiveMessagesTask: problem while reading message, aborting. " << endl;
    std::lock_guard<std::mutex> lock(_m);
    if (_socketToState[sockfd]->oneExited) {
        close(sockfd);
        free(_socketToState[sockfd]);
        _socketToState.erase(sockfd); // not necessary, but saves a bit of memory
        cout << "receiver freed socket " << sockfd << endl;
    } else {
        _socketToState[sockfd]->oneExited = true;
        cout << "receiver will let sender free socket " << sockfd << endl;
    }
}


/**
 * worker task: continually send messages when they're put in your queue. 
 */
void Messenger::sendMessagesTask(int sockfd) {
    // todo: these accesses are vulnerable to the overall map being reallocated, no? 
    BlockingQueue<std::string>& outBoundMessages = _socketToState[sockfd]->outboundMessages;
    std::string& hostAndPort = _socketToState[sockfd]->hostAndPort;

    while(true) {
        // wait for a message to be ready to be sent
        std::string message = outBoundMessages.blockingPop();

        // send message length 
        int msgLength = message.length();
        int n = sendEntireMessage(sockfd, &msgLength, sizeof(msgLength));
        if (n != sizeof(msgLength)) break;

        // send message body
        n = sendEntireMessage(sockfd, message.c_str(), message.length());
        if (n != message.length()) break;

        cout << "sender: sent a message with length " << message.length() << endl;
    }

    // we broke from the loop due to an error. commence abort sequence. 
    std::lock_guard<std::mutex> lock(_m);
    _hostAndPortToFd.erase(hostAndPort); // map can only contain live connections
    if (_socketToState[sockfd]->oneExited) {
        close(sockfd);
        free(_socketToState[sockfd]);
        _socketToState.erase(sockfd); // not necessary, but saves a bit of memory
        cout << "sender freed socket " << sockfd << endl;
    } else {
        _socketToState[sockfd]->oneExited = true;
        cout << "sender will let receiver free socket " << sockfd << endl;
    }
}


/**
 * Send a message to the designated address. 
 * 
 * Returns true if the connection was healthy for the send, false if the 
 * connection cound't be made. This method does NOT return true if the message
 * was sent successfully - it instead operates on a best-effort basis. 
 * 
 * If there is not an existing connection to the peer, one will be made. 
 * If sending a message fails, the connection is scrapped and the socket is closed. 
 */
bool Messenger::sendRequest(std::string hostAndPort, std::string message) { 
    // make a connection if it's the first time sending to this address
    int sockfd;
    std::lock_guard<std::mutex> lock(_m);
    if (!_hostAndPortToFd.count(hostAndPort)) {
        sockfd = establishConnection(hostAndPort);
        if (sockfd == -1) {
            cout << "connection failed to " << hostAndPort << endl;
            return false;
        }
        cout << "connection established with " << hostAndPort << endl;
        _hostAndPortToFd[hostAndPort] = sockfd;

        // create shared state for this socket
        _socketToState[sockfd] = new SocketState{};
        _socketToState[sockfd]->hostAndPort = hostAndPort;

        // create a request sender for this socket's lifetime
        std::thread sender(&Messenger::sendMessagesTask, this, sockfd);
        sender.detach();

        // create a response receiver for this socket's lifetime
        std::thread reader(&Messenger::receiveMessagesTask, this, sockfd, false);
        reader.detach();
    }
    sockfd = _hostAndPortToFd[hostAndPort];


    // pass the message to the socket's desigated sender 
    _socketToState[sockfd]->outboundMessages.blockingPush(message);
    return true;
}

/** 
 * Return a message if one is available.
 */
std::optional<Messenger::Request> Messenger::getNextRequest(int timeout) {
    std::optional<Request> msgOpt = _requestQueue.blockingPop_timed(timeout);

    if (msgOpt) {
        return *msgOpt;
    } else {
        return std::nullopt;
    }
}

/**
 * Send a response to a request you've already received. 
 *
 * Return true if the message was sent "best-effort", or false if there was
 * an issue sending or the destination socket was closed in the interim. 
 */
bool Messenger::sendResponse(Request::ResponseToken responseToken, std::string message) {
    // perform lookup w/ responseToken to see if the socket we received the message on is the same
    if (_socketToState[responseToken.sockfd]->timeCreated > responseToken.timestamp) {
        // the socket that we got the request on has been replaced! scrap the message
        cout << "sendResponse: Can't respond to stale socket, aborting" << endl;
        return false;
    }

    // pass the message to the socket's desigated sender 
    _socketToState[responseToken.sockfd]->outboundMessages.blockingPush(message);
    return true;
}


/** 
 * Return a message if one is available.
 */
std::optional<std::string> Messenger::getNextResponse(int timeout) {
    std::optional<std::string> msgOpt = _responseQueue.blockingPop_timed(timeout);
    if (msgOpt) {
        return *msgOpt;
    } else {
        return std::nullopt;
    }
}

// todo: implement
std::optional<std::string> Messenger::awaitResponseFrom(std::string hostAndPort, int timeout) {
    cout << "awaitResponseFrom: not implemented yet" << endl;
    return std::nullopt;
}


/**
 * Create a listening socket on the designated port. 
 *
 * Returns the created socket. 
 */
int createListeningSocket(int port) {
    sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(port);

    int listenfd;
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Error : socket() failed \n");
        exit(EXIT_FAILURE);
    } 
    // to prevent 'bind() failed: address already in use' errors on restarting
    int enable = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(int)) < 0) {
        perror("\n Error : setsockopt() failed \n");
        exit(EXIT_FAILURE);
    }
    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("\n Error : bind() failed \n");
        exit(EXIT_FAILURE);
    }
    int MAX_BACKLOG_CONNECTIONS = 20;
    if (listen(listenfd, MAX_BACKLOG_CONNECTIONS) < 0) {
        perror("\n Error : listen() failed \n");
        exit(EXIT_FAILURE);
    }
    return listenfd;
}

// temporary establish connection method, in terms of "IP:port"
// IP should be in standard IPv4 dotted decimal notation, ex. "127.0.0.95"
// example: "127.0.0.95:8000"
int establishConnection(std::string hostAndPort) {
    // parse input string 
    int colonIdx = hostAndPort.find(":");
    std::string IPstr = hostAndPort.substr(0, colonIdx);
    int port = std::stoi(hostAndPort.substr(colonIdx + 1));

    sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; // use IPv4
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(IPstr.c_str()); // translate str IP
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
        perror("bad address.");
        return -1;
    }

    // make the connection
    int connfd;
    if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Error : socket() failed \n");
        return -1;
    } 
    if(connect(connfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("\n Error : connect() failed \n");
        return -1;
    } 

    return connfd; 
}


/** 
 * Send 'length' bytes from a socket. Returns the number of bytes
 * read, or -1 if there was an error.
 */
int sendEntireMessage(const int connfd, const void* buf, const int length) { 
    int bytesWritten = 0;
    while (bytesWritten < length) {
        // 'MSG_NOSIGNAL': disable sigpipe in case write fails
        int n = send(connfd, (char *)buf + bytesWritten, 
                     length - bytesWritten, MSG_NOSIGNAL);
        if (n < 0) {
            return -1; 
        }
        bytesWritten += n;
    }
    return bytesWritten;
}


/** 
 * Read 'bytesToRead' bytes from a socket. Returns the number of bytes
 * read, or -1 if there was an error.
 */
int readEntireMessage(const int connfd, void* buf, int bytesToRead) {
    int bytesRead = 0;
    while (bytesRead < bytesToRead) {
        int n = recv(connfd, (char *)buf + bytesRead, 
                     bytesToRead - bytesRead, MSG_NOSIGNAL);
        // orderly shutdown, or an error ocurred
        if (n == 0 || n < 0) {
            return -1; 
        }
        bytesRead += n;
    }
    return bytesRead;
}
