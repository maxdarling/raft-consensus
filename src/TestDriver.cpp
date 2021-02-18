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


        // check for received messages
        int timeoutMs = 100;
        std::optional<Messenger::Request> requestOpt = messenger.getNextRequest(timeoutMs);
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
    
        sleep(5);
    }
}


// server #1 is client, server#2 is server
void clientServerTester(int serverNumber) {
    if (serverNumber == 1) {
        // client logic
        Messenger clientMessenger;
        std::string peerHostAndPort = "0:5002";
        int n_requests_sent = 0;
        while(true) {
            // send a request
            std::string requestMessage = "\n\n~~~~~~Message #" + std::to_string(++n_requests_sent) + 
                                " from server #" + std::to_string(serverNumber) + "~~~~~~~\n\n";

            clientMessenger.sendRequest(peerHostAndPort, requestMessage);
            clientMessenger.sendResponse({}, "fake baby"); // test invalid client operation

            // wait for a response
            std::optional<std::string> responseOpt = clientMessenger.getNextResponse(100); // for now, 100 does nothing. 
            if (responseOpt) {
                cout << "Response received: " << endl;
                cout << *responseOpt << endl;
            } else {
                cout << "No response message arrived in time" << endl;
            }

            sleep(5);
        }
    } 
    else {
        // server logic
        int myPort = PORT_BASE + serverNumber;
        Messenger serverMessenger(myPort);

        int n_responses_sent = 0;
        while(true) {
            // sit around for a request
            std::optional<Messenger::Request> requestOpt = serverMessenger.getNextRequest(100); // 100 does nothing
            if (requestOpt) {
                cout << "Request received: " << endl;
                cout << requestOpt->message << endl;

                //send a response  
                std::string responseMsg = "\n\n~~~~~~Response #" + std::to_string(++n_responses_sent) + 
                                    " from server #" + std::to_string(serverNumber) + "~~~~~~~\n\n";
                serverMessenger.sendResponse(requestOpt->responseToken, responseMsg);

            } else {
                cout << "No request message arrived in time" << endl;
            }

            sleep(5);
        } 
    }

}

int main(int argc, char* argv[])
{
    int serverNumber = std::stoi(argv[1]);
    //MessengerTester(serverNumber);
    clientServerTester(serverNumber);

    cout << "Program End" << endl;
    return 0;
}
