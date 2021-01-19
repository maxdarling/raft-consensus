/* 
    impl. notes:
    -can't listen and connect on same socket
    -check out setsockopt() in geeksforgeeks()

    -current map technique saves us from remaking sockets when we repeatedly send messages, 
    but we still make a new one for each received message, as accept() creates a new 
    socket each time. rip. we might need to rethink this. wait...or not. if we alreadly have the map
    entry, we should be writing to the fd. so we're good...?  ah, yes, the receive is broken then. send 
    with the map stops sending connect() calls, so there will be no effect from receive calling accept(). 
    receive must instead check and read on it's open fds??? tough.  
        -also, we might want to consider a protocol for delimiting the messages...? right now receive() will
        just read bytes, which could be .5 a message, 1.5 messages, etc. Perhaps we can make Messenger deal 
        with this..? 


    protocol buffer notes: 
    -create a filebuf by calling open on an fd. then create an iostream w/ filebuf as constructor arg. 
    -with that, we can ParseFromIstream(my_iostream) and SerialzeToOstream(my_iostream)

    todo: 
    -perhaps change send()/receive() using vector<char>
    -read up on linux accept, there might be cool automation to try:  
    "In order to be notified of incoming connections on a socket, 
     you can use select(2), poll(2), or epoll(7)." - linux accept() docs

     -design choice: it's tempting to add a method to conect to all other raft servers.
     because we'd like to have all connections in place, then start the main server loop 
     w/ timers, etc. should we provide a method for that here? yeah, we can just call it 
     'establishConnection' or something, and the Messenger will call it on each server 
     on file. 
*/

#include "Networker.h"

/* for socket() and its flags */
#include <sys/socket.h>
#include <sys/types.h> 

/* sockaddr_in */
#include <netinet/in.h>

/* for write() and read() */
#include <unistd.h>

const short MAX_CONNECTIONS = 10;


/* Setup a socket on the given port.
 *
 * After the following, future calls to send() and receive() 
 * will use the port, server address, and listen fd, which are set here. */
Networker::Networker(const short port) {
    memset(&_addr, '0', sizeof(_addr));

    // create socket
    if((_listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("\n Error : socket() failed \n");
        exit(EXIT_FAILURE);
    } 

    // specify server details, bind socket
    _addr.sin_family = AF_INET; // use IPv4
    _addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    _addr.sin_port = htons(port);

    if (bind(_listenfd, (struct sockaddr*)&_addr, sizeof(_addr)) < 0) {
        perror("\n Error : bind() failed \n");
        exit(EXIT_FAILURE);
    }

    // listen
    if (listen(_listenfd, MAX_CONNECTIONS) < 0) {
        perror("\n Error : listen() failed \n");
        exit(EXIT_FAILURE);
    }
}


/* Send bytes to a server at the specified address. 
 *
 * If a connection hasn't been established, creates a new one and 
 * saves it for re-use. 
 * 
 * Note: method is 'void' over 'bool' because we'd 
 * rather crash in the rare failure case than make the caller check for failure. */
void Networker::send(const struct sockaddr_in& serv_addr, const vector<char>& message) {
    int connfd;
    // create a connection if it doesn't already exist
    if (!_currConnections.count(serv_addr)) {
        if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("\n Error : socket() failed \n");
            exit(EXIT_FAILURE);
        } 
        if(connect(connfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("\n Error : connect() failed \n");
            exit(EXIT_FAILURE);
        } 
        _currConnections[serv_addr] = connfd;
    }

    connfd = _currConnections[serv_addr];

    int bytes_written = 0;
    while (bytes_written < message.size()) {
        int n = write(connfd, (char*)&message + bytes_written, message.size() - bytes_written);
        if (n < 0) {
            perror("\n Error : write() failed \n");
            exit(EXIT_FAILURE);
        }
        bytes_written += n;
    }
}


/* Recieve queued messages (if any). 
 *
 * If this is the first communication with the sender, we save it for re-use. 
 * Currently, this call blocks, which the caller must be aware of.  */
vector<char> Networker::receive() {
    struct sockaddr_in serv_addr;
    socklen_t addrlen = sizeof(serv_addr);
    //int connfd = accept4(_listenfd, (struct sockaddr *)&serv_addr, &addrlen, SOCK_NONBLOCK);
    // todo: this call is blocking, above is fix, but won't work. fnctl() may fix below? 
    int connfd = accept(_listenfd, (struct sockaddr *)&serv_addr, &addrlen); 

    // save this connection for future use
    _currConnections[serv_addr] = connfd;

    vector<char> result;
    char buf [1024];
    int n;
    while (n = read(_listenfd, buf, sizeof(buf)) > 0) {
        result.insert(result.end(), &buf[0], &buf[n]);
    }

    return result;
}