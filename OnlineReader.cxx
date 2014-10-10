#ifndef ONLINEREADER_H
#define ONLINEREADER_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "controlhost/choo.h"

int main(int argc, char **argv)
{
   std::cout << "test";    
    
   ControlHost *ch1 = new ControlHost ("auge", CHOO_WRITE, "Conn 1");
}

#endif
