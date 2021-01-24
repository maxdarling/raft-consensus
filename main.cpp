#include <stdio.h>
#include <poll.h>
#include <thread>
#include <iostream>
#include <unistd.h>

#include "Networker.h"

using std::cout;
using std::endl;

const int PORT_BASE = 5000;
const int FIRST_SERVER_NUMBER = 1;
const int LAST_SERVER_NUMBER = 2;


// todo: instead of using a sleep-based approach to boot all the servers and 
// connect them, we could use a count-based approach, in which the servers
// would remain in that loop and continue to issue 'connect()' calls until 
// all of them worked. 

int main(int argc, char* argv[])
{
    int serverNumber = std::stoi(argv[1]);
    int port = PORT_BASE + serverNumber; 
    Networker networker(port);
    
    cout << "Server #" << serverNumber << " has started" << endl;

    // configure IP / port for remote connection
    struct sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(port);

    cout << "Waiting 10s till the rest of the servers are started..." << endl;
    sleep(10);

    unordered_map<int, int> serverNumberToFd;

    // connect to the other servers
    for (int i = FIRST_SERVER_NUMBER; i <= LAST_SERVER_NUMBER; ++i) {
        if (i != serverNumber) {
            addr.sin_port = htons(PORT_BASE + i);
            int connfd = networker.establishConnection(addr);
            serverNumberToFd[i] = connfd;
            cout << "Successfully connected to server #" << i << endl;
        }
    }

    // test sending / receiving bytes
    int n_messages_sent = 0;
    while(true) {
        int readfd;
        if ((readfd = networker.getNextReadableFd()) != -1) {
            char buf [1024];
            int n = read(readfd, buf, sizeof(buf));
            std::string s(buf, n);  
            cout << "the following was received on server " << serverNumber << ":" << endl;
            cout << s << endl;
        }

        // send a message on a timer
        int RAND_DELAY = rand() % 15;
        sleep(RAND_DELAY);
        
        char buf [25];
        sprintf(buf, "Message #%d from server#%d", n_messages_sent, serverNumber);
        
        // send 1 message to each peer server
        for (auto it = serverNumberToFd.begin(); it != serverNumberToFd.end(); ++it) {
            int peerServerNumber = it->first; 
            cout << "peerServerNumber = " << peerServerNumber << endl;
            int serverFd = serverNumberToFd[peerServerNumber]; 
            cout << "serverFd = " << serverFd << endl;
            networker.sendAll(serverFd, buf, sizeof(buf));
            ++n_messages_sent;
        }
    }

    return 0;
}
