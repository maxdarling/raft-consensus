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
#include <iostream>
#include <thread>
#include <chrono>

using std::cout, std::endl;
using std::chrono::steady_clock;

/* convenience methods */
int createListeningSocket(int port);
int establishConnection(std::string peerAddr);
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
 * Creates a server instance with a listener on the given port.
 * 
 * Note: port should be in host-byte order. 
 */
Messenger::Messenger(const int listenPort) {
    // start a background thread to listen for connections
    _listenSock = createListeningSocket(listenPort);
    cout << "listening socket is " << *_listenSock << endl;
    std::thread listener(&Messenger::listener, this);
    listener.detach();
}

/**
 * Creates a client instance.
 */
Messenger::Messenger() {
}

/**
 * Messenger destructor. 
 */
Messenger::~Messenger() {
    // close listener so it can exit (only for server instances)
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
            perror("listener(): 'accept()' failed\n");
            if (errno == EBADF) {
                cout << "listener(): socket was closed, exiting" << endl;
                return;
            }
            cout << "listener(): non-fatal error, continuing" << endl;
            continue;
        }
        cout << "accepted connection on socket " << sockfd << endl;

        // create a new shared state variable for this socket 
        std::lock_guard<std::mutex> lock(_m);
        assert(!_socketToState.count(sockfd)); // ensure earlier cleanup was atomic
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
 * @param shouldReadRequests true if requests should be parsed, false for responses. 
 */
void Messenger::receiveMessagesTask(int sockfd, bool shouldReadRequests) {
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
        if (shouldReadRequests) { 
            Request request(message, sockfd, steady_clock::now(), *this);
            _requestQueue.notifyingPush(request);
        } else {
            _responseQueue.notifyingPush(message);
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
        cout << "receiver: cleaned up socket " << sockfd << endl;
    } else {
        _socketToState[sockfd]->oneExited = true;
        /* wakeup the sender */
        _socketToState[sockfd]->outboundMessages.notifyingPush("");
        cout << "receiver: will let sender cleanup socket " << sockfd << endl;
    }
}


/**
 * Worker task for 'sender' threads in which the thread will perpetually wait 
 * for messages to send to the given socket, exiting only once the socket dies.
 */
void Messenger::sendMessagesTask(int sockfd) {
    _m.lock();
    BlockingQueue<std::string>& outBoundMessages = _socketToState[sockfd]->outboundMessages;
    _m.unlock(); // todo: need this? is it sufficient? ask in OH

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

        // send message length 
        int msgLength = message.length();
        int n = sendEntireMessage(sockfd, &msgLength, sizeof(msgLength));
        if (n != sizeof(msgLength)) {
            break;
        }

        // send message body
        n = sendEntireMessage(sockfd, message.c_str(), message.length());
        if (n != message.length()) {
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
        cout << "sender: cleaned up socket " << sockfd << endl;
    } else {
        _socketToState[sockfd]->oneExited = true;
        cout << "sender: will let receiver cleanup socket " << sockfd << endl;
    }
}


/**
 * Send a request message to the designated address. 
 * 
 * 'peerAddr': "<a valid IPv4 dotted address>:<port>"
 *                example: "127.0.0.95:8000" 
 * 
 * Returns true if the message was sent via "best-effort", or false if there was
 * an issue during connection.  
 * 
 * If there is not an existing connection to the peer, one will be made. 
 * If sending a message fails, the connection socket is closed. 
 */
bool Messenger::sendRequest(std::string peerAddr, std::string message) { 
    int sockfd;
    std::lock_guard<std::mutex> lock(_m);
    // make a connection if it's the first time sending to this address
    if (!_peerAddrToSocket.count(peerAddr)) {
        sockfd = establishConnection(peerAddr);
        if (sockfd == -1) {
            cout << "connection failed to " << peerAddr << endl;
            return false;
        }
        cout << "connection established with " << peerAddr << endl;
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
    _socketToState[sockfd]->outboundMessages.notifyingPush(message);
    return true;
}


/**
 * Send a response to the peer who's request you've received. 
 *
 * Multiple responses can be sent using the same request object, as long as the
 * network connection hasn't been closed since the time of request receipt.
 * 
 * Return true if the message was sent via "best-effort", or false if the 
 * network connection was closed. 
 */
bool Messenger::Request::sendResponse(std::string message) {
    std::lock_guard<std::mutex> lock(_messengerParent._m);
    if (_messengerParent._socketToState[_sockfd]->timeCreated > _timestamp) {
        cout << "sendResponse: can't respond, connection to"
                "requester was closed" << endl;
        return false;
    }

    // pass the message to the socket's desigated sender 
    _messengerParent.
        _socketToState[_sockfd]->outboundMessages.notifyingPush(message);
    return true;
}


/** 
 * Return a request message if one becomes available in the specified duration, 
 * in milliseconds. 
 *
 * A negative timeout indicates an indefinite timeout.
 */
std::optional<Messenger::Request> Messenger::getNextRequest(int timeoutDurationMs) {
    if (timeoutDurationMs < 0) {
        return _requestQueue.waitingPop();
    }
    return _requestQueue.waitingPop_timed(timeoutDurationMs);
}


/** 
 * Return a response message if one becomes available in the specified duration, 
 * in milliseconds. 
 *
 * A negative timeout indicates an indefinite timeout.
 */
std::optional<std::string> Messenger::getNextResponse(int timeoutDurationMs) {
    if (timeoutDurationMs < 0) {
        return _responseQueue.waitingPop();
    }
    return _responseQueue.waitingPop_timed(timeoutDurationMs);
}


/**
 * Create a listening socket on the designated port (in host-byte order). 
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

/**
 * Create a socket connection to the designated peer address. 
 * The string is expected to look like "<IP>:<port>", where the port is in 
 * IPv4 decimal notation. A valid example input is "127.0.0.95:8000". 
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
 * Write 'length' bytes to a socket. Returns the number of bytes
 * written, or -1 if there was an error.
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
        // 'MSG_NOSIGNAL': disable sigpipe in case write fails
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
