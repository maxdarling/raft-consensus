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
    int n_requests_sent = 0;
    int n_responses_sent = 0;
    while(true) {
        // pick a random server to send a message to
        int peerServerNumber; 
        while ( (peerServerNumber = 1 + (rand() % nServers)) == serverNumber);

        std::string peerHostAndPort = "0:" + std::to_string(PORT_BASE + peerServerNumber);
        
        // construct a message
        std::string requestMessage = "\n\n~~~~~~Message #" + std::to_string(++n_requests_sent) + 
                              " from server #" + std::to_string(serverNumber) + "~~~~~~~\n\n";
 

        // get any responses
        std::optional<std::string> responseOpt = messenger.getNextResponse(100);
        if (responseOpt) {
            cout << "Response received: " << endl;
            cout << *responseOpt << endl;
        }
        
        // send the message
        messenger.sendRequest(peerHostAndPort, requestMessage);

       sleep(5);

        // check for received messages
        std::optional<Messenger::Request> requestOpt = messenger.getNextRequest();
        if (requestOpt) {
            cout << "Request received:" << endl;
            cout << (requestOpt->message) << endl; 
            // try to send a response, too!
            std::string responseMsg = "\n\n~~~~~~Response #" + std::to_string(++n_responses_sent) + 
                                " from server #" + std::to_string(serverNumber) + "~~~~~~~\n\n";
            messenger.sendResponse(requestOpt->responseToken, responseMsg);
        } else {
            cout << "No request message received in time" << endl;
        }
    }
}


// server #1 is client, server#2 is server
void clientServerTester(int serverNumber) {
    if (serverNumber == 1) {
        // client logic

    } 
    else {
        // server logic

    }
}

int main(int argc, char* argv[])
{
    int serverNumber = std::stoi(argv[1]);
    MessengerTester(serverNumber);

    cout << "Program End" << endl;
    return 0;
}
