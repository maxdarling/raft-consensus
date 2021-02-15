#include "Networker.h"

/* for 'socket()' and its flags */
#include <sys/socket.h>
#include <sys/types.h> 

/* for 'sockaddr_in' */
#include <netinet/in.h>

/* for 'close()' */
#include <unistd.h>

#include <poll.h>
#include <thread>
#include <cassert>
#include <iostream>

/* argument for 'listen()' */
const short MAX_BACKLOG_CONNECTIONS = 10;


/**
 * Background thread routine to accept incoming connection requeuests.
 */
void Networker::listenerRoutine() {
    while(true) {
        int connfd = accept(_listenfd, nullptr, nullptr); 

        // add this connection to be polled 
        std::lock_guard<std::mutex> lock(_m);
        _pfds.push_back({});
        _pfds.back().fd = connfd;
        _pfds.back().events = POLLIN;
    } 
}


/**
 * Activate the automatic handling of incoming connections.  
 * 
 * 'port' is the port to listen on in host-byte order. 
 */
Networker::Networker(const short port) {
    //initialize members
    memset(&_addr, '0', sizeof(_addr));
    _addr.sin_family = AF_INET; // use IPv4
    _addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    _addr.sin_port = htons(port);

    // create the dedicated listening socket
    if((_listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Error : socket() failed \n");
        exit(EXIT_FAILURE);
    } 
    // to prevent 'bind() failed: address already in use' errors on restarting
    int enable = 1;
    if (setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   &enable, sizeof(int)) < 0) {
        perror("\n Error : setsockopt() failed \n");
        exit(EXIT_FAILURE);
    }
    if (bind(_listenfd, (struct sockaddr*)&_addr, sizeof(_addr)) < 0) {
        perror("\n Error : bind() failed \n");
        exit(EXIT_FAILURE);
    }
    if (listen(_listenfd, MAX_BACKLOG_CONNECTIONS) < 0) {
        perror("\n Error : listen() failed \n");
        exit(EXIT_FAILURE);
    }

    // start the listener thread in the background
    std::thread th = std::thread(&Networker::listenerRoutine, this);
    th.detach();
}


/**
 * Return a file descriptor on which data is available to read, or -1 if 
 * no such descriptors are available.  
 * 
 * Temporarily non-blocking for refactoring 
 */
int Networker::getNextReadableFd() {
    std::lock_guard<std::mutex> lock(_m);
    int timeout = 0;
    if (poll(&_pfds[0], (nfds_t) _pfds.size(), timeout) == 0) {
        return -1;
    }

    // some fd's are now ready to read!
    for (int i = 0; i < _pfds.size(); ++i) {
        if (_pfds[i].revents & POLLIN) {
            _readableFds.push(_pfds[i].fd);
        }
    }

    int result_fd = _readableFds.front();
    _readableFds.pop();
    return result_fd;
}


/** 
 * Attempts to send 'length' bytes to the specified socket descriptor. 
 * Returns the # of bytes sent, or -1 on an error or closed connection.
 */
int Networker::sendAll(const int connfd, const void* buf, const int length) { 
    int bytesWritten = 0;
    while (bytesWritten < length) {
        /* we must check that the connection is still open before we attempt to
         * write to it. to determine this, we use the fact that 'recv()' will
         * read a '0' (an EOF) if the connection has been closed. 
         * 
         * note: in theory, it's possible that 'recv()' return -1 as an error
         * instead of the usual -1 produced by the DONTWAIT flag. We currently
         * do not handle this case, as we are not able to produce it.
         */
        int checkEOF = recv(connfd, nullptr, 1, MSG_DONTWAIT);
        if (checkEOF == 0) {
            return -1;
        }
        // flag: disable error signal handling for this call. 
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
 * Read 'bytesToRead' bytes from a peer's socket. Returns the number of bytes
 * read, or -1 if there was an error.
 * 
 * If the peer closed the connection or an error ocurred while reading, 
 * the socket will be closed.
 */
int Networker::readAll(const int connfd, void* buf, int bytesToRead) {
    int bytesRead = 0;
    while (bytesRead < bytesToRead) {
        int n = recv(connfd, (char *)buf + bytesRead, 
                     bytesToRead - bytesRead, MSG_NOSIGNAL);
        // orderly shutdown, or an error ocurred
        if (n == 0 || n < 0) {
            // todo: should remove from polling 
            return -1; 
        }
        bytesRead += n;
    }
    return bytesRead;
}
