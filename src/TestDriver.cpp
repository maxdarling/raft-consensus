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




void MessengerTester(int serverNumber) {
    // start Messenger
    int myPort = PORT_BASE + serverNumber;
    Messenger messenger(myPort);

    int nServers = 2;
    cout << "Server #" << serverNumber << " of " << nServers << " has started" << endl;

    // send messages
    int n_messages_sent = 0;
    while(true) {
        // pick a random server to send a message to
        int peerServerNumber; 
        while ( (peerServerNumber = 1 + (rand() % nServers)) == serverNumber);

        std::string peerHostAndPort = "0:" + std::to_string(PORT_BASE + peerServerNumber);
        
        // construct a message
        std::string message = "\n\n~~~~~~Message #" + std::to_string(++n_messages_sent) + 
                              " from server #" + std::to_string(serverNumber) + "~~~~~~~\n\n";
 
        
        // send the message
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
    MessengerTester(serverNumber);

    cout << "Program End" << endl;
    return 0;
}
