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

void readSocketThread(int cliSocket) {
    char buffer[300];
    do {
        int n = read(cliSocket, buffer, 300);
        if (n <= 0) break;
        buffer[n] = '\0';
        std::cout << "\nMensaje recibido: " << buffer << std::endl;
    } while (true);
    close(cliSocket);
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
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (-1 == connect(SocketFD, (struct sockaddr*)&stSockAddr, sizeof(stSockAddr))) {
        perror("Connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    std::cout << "Ingresa tu nombre: ";
    std::string name;
    std::getline(std::cin, name);
    write(SocketFD, name.c_str(), name.size() + 1);

    std::thread(readSocketThread, SocketFD).detach();

    while (true) {
        std::cout << "Ingrese mensaje (destinatario1,destinatario2:mensaje): ";
        std::string msg;
        std::getline(std::cin, msg);

        if (msg == "exit") break;
        write(SocketFD, msg.c_str(), msg.size() + 1);
    }

    close(SocketFD);
    return 0;
}
