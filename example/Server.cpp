#include "server.hpp"
#include <iostream>

int main(int argc, char *argv[])
{
    ModernWebServer server(argv[0]);

    if (!server.Initialize(argc, argv))
    {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }

    server.Start();

    return 0;
}