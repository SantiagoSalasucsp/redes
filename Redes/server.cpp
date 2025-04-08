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

using namespace std;

map<string, int> clients;

void handleClient(int cliSocket, string nickname)
{
    char buffer[256];
    while(true) {
        int n = read(cliSocket, buffer, 256);
        if(n <= 0) break;
        buffer[n] = '\0';

        if(buffer[0] == 'L') {
            string userList;
            for(auto& client : clients) {
                userList += client.first + " ";
            }
            write(cliSocket, userList.c_str(), userList.length());
        }
        else if(buffer[0] == 'M') {
            if(strncmp(buffer, "exit", 4) == 0) break;
            
            // Separar destinatario y mensaje
            string msg(buffer + 1);
            size_t space = msg.find(' ');
            if(space != string::npos) {
                string recipient = msg.substr(0, space);
                string content = msg.substr(space + 1);
                
                if(clients.find(recipient) != clients.end()) {
                    string fullMsg = nickname + ": " + content;
                    write(clients[recipient], fullMsg.c_str(), fullMsg.length());
                }
            }
        }
    }
    
    clients.erase(nickname);
    shutdown(cliSocket, SHUT_RDWR);
    close(cliSocket);
    cout << nickname << " desconectado" << endl;
}

int main(void)
{
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in stSockAddr;

    if(-1 == SocketFD) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(stSockAddr));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if(-1 == bind(SocketFD, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr))) {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if(-1 == listen(SocketFD, 10)) {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Servidor iniciado" << endl;

    while(true) {
        int ClientFD = accept(SocketFD, NULL, NULL);
        char nickname[256];
        int n = read(ClientFD, nickname, 256);
        if(n <= 0) {
            close(ClientFD);
            continue;
        }
        nickname[n] = '\0';

        clients[nickname] = ClientFD;
        cout << nickname << " conectado" << endl;
        thread(handleClient, ClientFD, nickname).detach();
    }

    close(SocketFD);
    return 0;
}