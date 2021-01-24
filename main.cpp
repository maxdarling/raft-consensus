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


int main(int argc, char* argv[])
{
    int serverNumber = std::stoi(argv[1]);
    int port = PORT_BASE + serverNumber; 
    Networker networker(port);
    
    cout << "finished constructing Networker" << endl;

    // configure IP / port for remote connection
    struct sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(port);

    cout << "sleeping for 10s" << endl;
    sleep(10);

    cout << "conn loop starting..." << endl;
    // connect to the other servers
    for (int i = FIRST_SERVER_NUMBER; i <= LAST_SERVER_NUMBER; ++i) {
        if (i != serverNumber) {
            cout << "about to attempt connecting to server " << i << endl;
            addr.sin_port = htons(PORT_BASE + i);
            networker.establishConnection(addr);
            cout <<  "Successfully connected to server #" << i << endl;
        }
    }

    // inf loop...
    while(true) {

    }


    return 0;
}
