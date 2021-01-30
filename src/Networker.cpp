#include "Networker.h"

/* for socket() and its flags */
#include <sys/socket.h>
#include <sys/types.h> 

/* for poll() */
#include <poll.h>

/* sockaddr_in */
#include <netinet/in.h>

/* for write() and read() */
#include <unistd.h>

#include <thread>

#include <cassert>

#include <iostream>

using std::cout;
using std::endl;

/* argument for 'listen()' */
const short MAX_BACKLOG_CONNECTIONS = 10;


/**
 * Background thread routine to establish incoming connection requeuests. 
 */
void Networker::listenerRoutine() {
    while(true) {
        int connfd = accept(_listenfd, nullptr, nullptr); 

        // add to set of polled connections
        std::lock_guard<std::mutex> lock(_m);
        if (_pfds_size == _pfds_capacity) {
            _pfds_capacity *= 2;
            _pfds = (struct pollfd*) realloc(_pfds, _pfds_capacity);
        }
        _pfds[_pfds_size].fd = connfd;
        _pfds[_pfds_size].events = POLLIN;
        ++_pfds_size;
    } 
}


/**
 * Spawn a background thread to listen for incoming connections.  
 */
Networker::Networker(const short port) {
    //initialize members
    memset(&_addr, '0', sizeof(_addr));
    _addr.sin_family = AF_INET; // use IPv4
    _addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    _addr.sin_port = htons(port);

    _pfds_size = 0;
    _pfds_capacity = 10;
    _pfds = (pollfd*) malloc(sizeof(pollfd) * _pfds_capacity);

    // create the dedicated listening socket
    if((_listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("\n Error : socket() failed \n");
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

    // start a listener thread in the background
    std::thread th = std::thread(&Networker::listenerRoutine, this);
    th.detach();
}


/** 
 * Cleanup resources on program exit. 
 */
Networker::~Networker() {
    free(_pfds);
}


/**
 * Establishes an outbound connection with a server at the specified address,
 * and returns the associated file descriptor. If the
 * connection fails, returns -1.
 * 
 */
int Networker::establishConnection(const struct sockaddr_in& serv_addr) {
    int connfd;
    if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        return -1;
    } 
    if(connect(connfd, (struct sockaddr *)&serv_addr, 
                sizeof(serv_addr)) < 0) {
        return -1;
    } 

    return connfd; 
}


/** 
 * Send all bytes to a connection with the specified file descriptor.  
 *
 * If a connection associated with the provided file descriptor doesn't exist, 
 * the behavior is undefined (write to an unknown fd).  
 * 
 */
int Networker::sendAll(const int connfd, const void* buf, const int length) { 
    // write the entire buffer's contents
    int bytesWritten = 0;
    while (bytesWritten < length) {
        /* we must check that the connection is still open before we attempt
           to write to it. to determine this, we use the fact that 'recv()' 
           will read a '0' (an EOF) if the connection has been closed. 

           note: it's also possible that 'recv()' return -1 as an error instead
           of the usual -1 produced by the DONTWAIT flag. We currently do not
           handle this case, as we are not able to produce it. 
        */
        int checkEOF = recv(connfd, nullptr, 1, MSG_DONTWAIT);
        cout << "sendAll eof: " << checkEOF << endl;
        if (checkEOF == 0) {
            cout << "EOF detected in sendAll()" << endl;
            return -1;
        }
        // flag: disable error signal handler for this call. 
        int n = send(connfd, (char *)buf + bytesWritten, length - bytesWritten, MSG_NOSIGNAL);
        if (n < 0) {
            cout << "sendAll() failed (but connection wasn't closed)" << endl;
            return -1; 
        }
        bytesWritten += n;
    }
    return bytesWritten;
}


/** 
 * Read 'bytesToRead' bytes from specified socket descriptor. Returns the 
 * number of bytes read, or -1 if there was an error.  
 * 
 * If the peer closed the connection or an error ocurred while reading, 
 * the socket will be closed.
 * 
 */
int Networker::readAll(const int connfd, void* buf, int bytesToRead) {
    int bytesRead = 0;
    while (bytesRead < bytesToRead) {
        int n = recv(connfd, (char *)buf + bytesRead, bytesToRead - bytesRead, 0/*MSG_NOSIGNAL*/);
        // orderly shutdown, or an error ocurred
        if (n == 0 || n < 0) {
            std::string errMsg = 
                (n == 0 ? "peer closed connection" : "readAll() failed");
            cout << errMsg << endl;
            // no longer poll this fd
            std::lock_guard<std::mutex> lock(_m);
            close(connfd);
            for (int i = 0; i < _pfds_size; ++i) {
                if (_pfds[i].fd == connfd) {
                    std::swap(_pfds[i], _pfds[_pfds_size - 1]);
                    --_pfds_size;
                }
            }
            return -1; 
        }
        bytesRead += n;
    }
    return bytesRead;
}


/**
 * Return a file descriptor on which data is available to read, or -1 if 
 * no such descriptors are available.  
 * 
 * The call can be made blocking or not based on the input param. 
 */
int Networker::getNextReadableFd(bool shouldBlock) {
    // if no readable fd's left, check for more with poll() 
    if (_readableFds.size() == 0) {
        int timeout; // (ms)
        if (shouldBlock) {
            timeout = 100; // must still use finite timeout to let pfds expand
            while (true) {
                std::lock_guard<std::mutex> lock(_m);
                if (poll(_pfds, _pfds_size, timeout) > 0) {
                    break;
                }
            }
        } else {
            std::lock_guard<std::mutex> lock(_m);
            timeout = 0;
            if (poll(_pfds, _pfds_size, timeout) == 0) {
                return -1;
            }
        }

        // some fd's are now ready to read!
        std::lock_guard<std::mutex> lock(_m);
        for (int i = 0; i < _pfds_size; ++i) {
            if (_pfds[i].revents & POLLIN) {
                _readableFds.push(_pfds[i].fd);
            }
        }
    }

    int result_fd = _readableFds.front();
    _readableFds.pop();
    return result_fd;
}
