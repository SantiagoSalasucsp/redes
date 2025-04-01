#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <map>

std::map<int, std::string> clientMap;

void readSocketThread(int cliSocket) {
    char buffer[300];
    do {
        int n = read(cliSocket, buffer, 300);
        if (n <= 0) break;

        std::string msg(buffer);
        std::cout << "Mensaje recibido: " << msg << std::endl;

        size_t colonPos = msg.find(':');
        if (colonPos != std::string::npos) {
            std::string recipients = msg.substr(0, colonPos);
            std::string content = msg.substr(colonPos + 1);

            for (auto& client : clientMap) {
                if (recipients == "all" || recipients.find(client.second) != std::string::npos) {
                    write(client.first, content.c_str(), content.size());
                }
            }
        }
    } while (true);

    close(cliSocket);
    clientMap.erase(cliSocket);
}

int main() {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr))) {
        perror("Bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10)) {
        perror("Listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    while (true) {
        int ClientFD = accept(SocketFD, NULL, NULL);
        if (ClientFD < 0) {
            perror("Accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }

        char clientName[100];
        read(ClientFD, clientName, 100);
        clientMap[ClientFD] = clientName;
        std::cout << "Nuevo cliente: " << clientName << std::endl;

        std::thread(readSocketThread, ClientFD).detach();
    }

    close(SocketFD);
    return 0;
}
