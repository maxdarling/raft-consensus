/* 
design notes: 
Q:How does the RAFT main-loop interact with networking? 
A: The prime consideration is around synchronization and blocking, and there are
many options. First, we have decided to make the networking calls
non-blocking. This "pulls complexity downward" and makes life easier for the
top-level client. Multi-threading must almost certainly exist somewhere, and
we are choosing it to exist at the bottom level, instead of at the top. This
allows the RAFT implementation to be entirely single-threaded if it wants.

The reason that blocking calls force the top-level raft client to use
multithreading is that if a blocking call takes longer than a timer - ex. the
election timeout for followers - then the algorithm implementation becomes
incorrect. In a more extreme case, there is a cold-start issue in which all N
servers connect, assume follower state, and block while waiting for messages
from each other, which never come. Perhaps a timed wait would solve this, but
that's more complex, since it'd require passing the desired time from the
raft client to the messenger and then to the networker.

implementaiton notes: 
1. todo: add locking where needed.

2. I decided that the API should be sendAll() and getNextReadableFd(). There
are a few things to address here. First, the networker should not be aware of
the "messages" concept, just bytes to be read / sent. Given this, it makes
sense that the burden of doing particular reading and delimitting will fall
on the messenger class. Since this will involve calls to 'read()', it makes
sense to be working in terms of fds, as opposed to something higher level
like IP/port.

3. Thinking ahead to raft, I know we're going to be doing single-threaded,
which is good, but I still see two different approaches: timer-based or
hot-loop. The timer-based approach is perhaps better because it saves
resources, but I need to make sure it's correct. But essentially we'd sleep
for the appropriate timer duration (election timeout for C/F, heartbeat for
L). Then on wakeup, we'd perform the algorithm steps and check if any RPCs
came. The ideal scenario is to couple this with an early wakeup if any RPCs
arrive, but that's going to require setting up some signals to be sent, which
seems difficult. In the worst case, hot-loop will work, so that's the good
news :)

4. For the delimiting messages implementation, the most simple approach to
follow is doing a single read for the first 4 bytes for the size of the
packet, and then doing a readAll() for that # of bytes after that. It's a
tiny bit of overhead in that it might end up adding +1 read() call each time,
but it makes the code super simple, and I don't care about performance enough
on my 1st time implementing something like this to care.

5. getNextReadableFd() and messenger usage pattern: As an example, lets say
that fd 5 is ready. We read 1 msg from it (blocking a bit if necessary).
Then, we need to check if another message is there. We should do this with a
non-blocking read() call of 4 bytes, I think... Or could we call poll() on it
with 0 timeout, which is kindof pog. 

More todos: 
-rename "sendAll" - seems like sending to all servers
-handle the possibility of dropped connections
-remove '_currConnections' if no longer needed
-more seriously consider the problem of 2 sockets created between a single 
pair of servers on startup.  
-epoll() instead of poll()? 
-later: think about IPv6

Assumptions:
-the blocking for reads/writes is acceptable for the client
    -messenger: if some bytes are ready to read, then the rest will be ready 
    soon enough that force-reading all is fine...(not true if server crashes
    in the middle of sending message).
    -sendAll() is safe. is this untrue if we're writing to a connection to
    a server that's crashed? 
*/

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

/* a limit argument for listen() */
const short MAX_BACKLOG_CONNECTIONS = 10;


/* Background thread routine to establish incoming connection requeuests. 
 *
 */
void Networker::listener_routine() {
    while(true) {
        // accept new connection and store client information in 'serv_addr'
        struct sockaddr_in serv_addr;
        memset(&serv_addr, '0', sizeof(serv_addr));
        socklen_t addrlen = sizeof(serv_addr); 
        int connfd = accept(_listenfd, (struct sockaddr *)&serv_addr, &addrlen); 
        
        // save this connection for future use
        std::lock_guard<std::mutex> lock(_m);
        if (_currConnections.count(serv_addr)) {
           /* concurrency case: if we're already connected to this server, we
            * must have completed a call to 'establishConnection()' before 
            * this. That necessitates that the peer server called 'accept()' on
            * us, so we can safely close this new connection. 
            * 
            * Note: it's also just easier to keep the 2 sockets and not close 
            * them, which we're doing now. Calling 'close()' might mean we 
            * need to handle the case where we close our peer's connection. 
            * Todo: map this on paper to see if that's even possible. */
        } else {
            _currConnections[serv_addr] = connfd;

            // add to set of polled connections
            if (_pfds_size == _pfds_capacity) {
                _pfds_capacity *= 2;
                _pfds = (struct pollfd*) realloc(_pfds, _pfds_capacity);
            }
            _pfds[_pfds_size].fd = connfd;
            _pfds[_pfds_size].events = POLLIN;
            ++_pfds_size;
        }

    } 
}


/* Spawn a background thread to listen for incoming connections.  
 *
 */
Networker::Networker(const short port) {
    // initialize members
    memset(&_addr, '0', sizeof(_addr));
    _addr.sin_family = AF_INET; // use IPv4
    _addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    _addr.sin_port = htons(port);

    // todo: figure out if we can use this, instead of non-hidden header code
    //auto cmp = [](const struct sockaddr_in& a, const struct sockaddr_in& b) {
    //    if (a.sin_port != b.sin_port) {
    //        return a.sin_port < b.sin_port;
    //    }
    //    return a.sin_addr.s_addr < b.sin_addr.s_addr;
    //};
    //_currConnections = map<sockaddr_in, int, decltype(cmp)>(cmp);

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

    // start listener thread in the background
    std::thread th(&Networker::listener_routine, this); // never .join()'ed, as it loops forever
    th.detach();
    //std::thread th(foo); // never .join()'ed, as it loops forever
    //_th = std::thread(&Networker::listener_routine, this);
}


/* Cleans up resources on program exit. 
 * 
 * Note: there's nothing to do for the thread, since it loops forever. */
Networker::~Networker() {
    free(_pfds);
}


/* Establishes a connection with a server at the specified address, and returns 
 * the associated file descriptor for communication. 
 * 
 * Note: if we already have made a connection with the server of interest (ie. 
 * we accept()'ed a connection from them already) then we will not create a new
 * socket connection, and instead recycle the old one. */
int Networker::establishConnection(const struct sockaddr_in& serv_addr) {
    int connfd;
    // create a connection if it doesn't already exist
    if (!_currConnections.count(serv_addr)) {
        if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("\n Error : socket() failed \n");
            exit(EXIT_FAILURE);
        } 
        if(connect(connfd, (struct sockaddr *)&serv_addr, 
                   sizeof(serv_addr)) < 0) {
            perror("\n Error : connect() failed \n");
            exit(EXIT_FAILURE);
        } 

        // save connection for future use
        std::lock_guard<std::mutex> lock(_m);
        if (_currConnections.count(serv_addr)) {
            // see corresponding concurency note above in 'listener_routine()'
        } else {
            _currConnections[serv_addr] = connfd;

            // add to set of polled connections
            if (_pfds_size == _pfds_capacity) {
                _pfds_capacity *= 2;
                _pfds = (struct pollfd*) realloc(_pfds, _pfds_capacity);
            }
            _pfds[_pfds_size].fd = connfd;
            _pfds[_pfds_size].events = POLLIN;
            ++_pfds_size;
        }
    }

    return _currConnections[serv_addr];
}


/* Send all bytes to a connection with the specified file descriptor.  
 *
 * If a connection associated with the provided file descriptor doesn't exist, 
 * the behavior is undefined (write to an unknown fd).  
 * 
 * Note: method is 'void' over 'bool' because we'd rather crash in the rare
 * failure case than make the caller check the validity of each call. */
void Networker::sendAll(const int connfd, const char* buf, const int length) { 
    // write the entire buffer's contents
    int bytes_written = 0;
    while (bytes_written < length) {
        int n = send(connfd, buf + bytes_written, length - bytes_written, 0);
        if (n < 0) {
            perror("\n Error : write() failed \n");
            exit(EXIT_FAILURE);
        }
        bytes_written += n;
    }
}


/* Return a file descriptor on which data is available to read, or -1 if 
 * no such descriptors are available.  
 * 
 * The call is designed to be non-blocking for caller convenience. 
 * 
 * Note: this method can also be implemented with a supplemental background
 * thread whose job is to call poll() in the background, but using a 
 * non-blocking poll() call here seemed simpler, if less efficient. */
int Networker::getNextReadableFd() {
    // if no readable fds left, check for more with poll() 
    if (_readableFds.size() == 0) {
        std::lock_guard<std::mutex> lock(_m);
        int n_ready = poll(_pfds, _pfds_size, 0); // '0' -> non-blocking
        if (n_ready == 0) {
            return -1;
        }
        for (int i = 0; i < _pfds_size; ++i) {
            if (_pfds[i].revents & POLLIN) {
                _readableFds.push(_pfds[i].fd);
            }
        }
    }

    int result_fd= _readableFds.front();
    _readableFds.pop();
    return result_fd;
}
