#include <stdio.h>
#include <poll.h>
#include <thread>
#include <iostream>
#include <unistd.h>

#include "Messenger.h"

using std::cout;
using std::endl;

const int PORT_BASE = 5000;
const int FIRST_SERVER_NUMBER = 1;
const int LAST_SERVER_NUMBER = 2;


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ATTENTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    This file should be ignored for CS190 code reviews. 






   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ATTENTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */


void NetworkerTester(int serverNumber) {
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
        if ((readfd = networker.getNextReadableFd(false)) != -1) {
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


}

void MessengerTester(int serverNumber) {
    int port = PORT_BASE + serverNumber; 
    cout << "Server #" << serverNumber << " has started" << endl;

    // define the server list
    struct sockaddr_in addr;
    memset(&addr, '0', sizeof(addr));
    addr.sin_family = AF_INET; // use IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // use local IP
    addr.sin_port = htons(PORT_BASE);

    int nServers = 2;
    // explicit initialization because loop version is less easy to visualize
    unordered_map<int, struct sockaddr_in> serverList {
        {1, addr},
        {2, addr}
    };
    for (auto it = serverList.begin(); it != serverList.end(); ++it) {
        it->second.sin_port = htons(PORT_BASE + it->first);
        cout << "created server list entry. ID = "<< it->first << ",  port = " << it->second.sin_port << endl;
    }

    // start Messenger
    int myPort = PORT_BASE + serverNumber;
    Messenger messenger(myPort);

    // send messages
    int n_messages_sent = 0;
    while(true) {
        // pick a random server to send a message to
        int peerServerNumber; 
        while ((peerServerNumber = 1 + (rand() % serverList.size())) == serverNumber);

        std::string peerHostAndPort = "0:" + std::to_string(PORT_BASE + peerServerNumber);
        
        // construct a message
        std::string message = "\n\n~~~~~~Message #" + std::to_string(++n_messages_sent) + 
                              " from server #" + std::to_string(serverNumber) + "~~~~~~~\n\n";
 
        
        // send the message
        //messenger.sendMessageToServer(peerServerNumber, message);
        messenger.sendMessage(peerHostAndPort, message);

       sleep(5);

        // check for received messages
        std::optional<std::string> incMsgWrapper = messenger.getNextMessage();
        if (incMsgWrapper) {
            cout << "Message received:" << endl;
            cout << (*incMsgWrapper) << endl; 
        }
    }
}

int main(int argc, char* argv[])
{
    int serverNumber = std::stoi(argv[1]);
    //NetworkerTester(serverNumber);
    MessengerTester(serverNumber);

    cout << "Program End" << endl;
    return 0;
}
