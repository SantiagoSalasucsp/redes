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

using namespace std;

void readSocketThread(int cliSocket)
{
    char buffer[300];
    do {
        int n = read(cliSocket, buffer, 300);
        if(n <= 0) break;
        buffer[n] = '\0';
        cout << buffer << endl;
        cout << "Opciones:\n1. Listar usuarios (L)\n2. Enviar mensaje (M)\n3. Salir (exit)\n";
        cout << "Ingrese opción: ";
        fflush(stdout);
    } while(true);
    
    shutdown(cliSocket, SHUT_RDWR);
    close(cliSocket);
}

int main(void)
{
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    string nickname;

    if(-1 == SocketFD) {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if(-1 == connect(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr))) {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Ingrese su nickname: ";
    getline(cin, nickname);
    write(SocketFD, nickname.c_str(), nickname.length());

    thread(readSocketThread, SocketFD).detach();

    while(true) {
        cout << "Opciones:\n1. Listar usuarios (L)\n2. Enviar mensaje (M)\n3. Salir (exit)\n";
        cout << "Ingrese opción: ";
        string option;
        getline(cin, option);

        if(option == "1" || option == "L") {
            write(SocketFD, "L", 1);
        }
        else if(option == "2" || option == "M") {
            string recipient, message;
            cout << "Destinatario: ";
            getline(cin, recipient);
            cout << "Mensaje: ";
            getline(cin, message);
            string msg = "M" + recipient + " " + message;
            write(SocketFD, msg.c_str(), msg.length());
        }
        else if(option == "3" || option == "exit") {
            write(SocketFD, "exit", 4);
            break;
        }
    }

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}