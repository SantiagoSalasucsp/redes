/* Client code in C */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <string>
#include <cstring> 

using namespace std;

void readSocketThread(int cliSocket)
{
    char buffer[300];
    do
    {
        int n = read(cliSocket, buffer, 300);
        if (n <= 0) break;
        buffer[n] = '\0';
        cout << buffer << endl;
    } while (true);
    close(cliSocket);
}

int main(void)
{
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45001);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    string nombre;
    cout << "Ingrese su nombre: ";
    cin >> nombre;
    cin.ignore(); 

    
    write(SocketFD, nombre.c_str(), nombre.size());

    thread(readSocketThread, SocketFD).detach();
    
    cout << "nombre,nombre:mensaje" << endl;
    
    do
    {
        cout << "> ";
        string mensajeCompleto;
        getline(cin, mensajeCompleto);
        
        if (mensajeCompleto == "exit") break;
        
        write(SocketFD, mensajeCompleto.c_str(), mensajeCompleto.size());
    } while (true);

    close(SocketFD);
    return 0;
}
