#ifndef ONLINEREADER_H
#define ONLINEREADER_H

#include <iostream>
#include <string>

#include "controlhost/choo.h"

void connect(ControlHost *& ch) {
    std::cout << "Creating control host client." << std::endl;
    std::string subscription = "a MSG";
    ControlHost::Throw(true);
    try {
        ch = new ControlHost ("131.188.167.62", 5553);
        ch->MyId("OnlineReader");
        ch->Subscribe(subscription);
        ch->SendMeAlways();
        std::cout << "Client ready." << std::endl;
    } catch (ControlHost::Exception error) {
        std::cout << "Connecting failed." << std::endl;
        delete ch;
        ch = NULL;
    }
}

int main(int argc, char **argv)
{
    ControlHost *ch = NULL;	
    ControlHost::Throw(true); 

    connect(ch);

    std::cout << "Checking connection." << std::endl;
    if (ch->Connected()) {
        std::cout << "Connected to Ligier" << std::endl;
    } else {
        std::cout << "Could not connect to Ligier. Exiting..." << std::endl;
        return -1;
    }
    
    std::string tag;
    int nbytes = 0;

    ch->WaitHead(tag, nbytes);
    
    std::cout << "Got a tag " << std::endl;
    if (tag == "FOO") {
        std::cout << "Got FOO!" << std::endl;
    //    ch->GetFullData(buffer, nbytes);
    }
}


#endif
