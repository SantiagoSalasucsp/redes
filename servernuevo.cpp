/* Server code in C */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <list>
#include <vector>
#include <algorithm>
#include <cstring> 

using namespace std;


struct Cliente {
    int socket;
    string nombre;
};

list<Cliente> clientesConectados;

void readSocketThread(int cliSocket)
{
    char buffer[300];
   
    int n = read(cliSocket, buffer, 100);
    if (n <= 0) {
        close(cliSocket);
        return;
    }
    buffer[n] = '\0';
    string nombreCliente(buffer);
    
    
    clientesConectados.push_back({cliSocket, nombreCliente});
    cout << "Cliente conectado: " << nombreCliente << endl;

    do {
        n = read(cliSocket, buffer, 300);
        if (n <= 0) break;
        buffer[n] = '\0';
        
        string mensajeCompleto(buffer);
        size_t posSeparador = mensajeCompleto.find(':');
        
        if (posSeparador != string::npos) {
            string destinatariosStr = mensajeCompleto.substr(0, posSeparador);
            string mensaje = mensajeCompleto.substr(posSeparador + 1);
            
            
            vector<string> destinatarios;
            size_t pos = 0;
            while ((pos = destinatariosStr.find(',')) != string::npos) {
                string dest = destinatariosStr.substr(0, pos);
                if (!dest.empty()) destinatarios.push_back(dest);
                destinatariosStr.erase(0, pos + 1);
            }
            if (!destinatariosStr.empty()) {
                destinatarios.push_back(destinatariosStr);
            }
            
            
            string mensajeFinal = nombreCliente + " dice: " + mensaje;
            
            /
            for (const auto& cliente : clientesConectados) {
                bool enviar = destinatarios.empty(); 
                
                if (!enviar) {
                    
                    enviar = find(destinatarios.begin(), destinatarios.end(), cliente.nombre) != destinatarios.end();
                }
                
                if (enviar && cliente.socket != cliSocket) {
                    write(cliente.socket, mensajeFinal.c_str(), mensajeFinal.size());
                }
            }
        }
    } while (true);

    
    clientesConectados.remove_if([cliSocket](const Cliente& c) { return c.socket == cliSocket; });
    cout << "Cliente desconectado: " << nombreCliente << endl;
    close(cliSocket);
}

int main(void)
{
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (-1 == SocketFD)
    {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in stSockAddr;
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45001);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("error bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10))
    {
        perror("error listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    cout << "Servidor iniciado en el puerto 45001. Esperando conexiones..." << endl;

    while (true)
    {
        int ClientFD = accept(SocketFD, NULL, NULL);

        if (0 > ClientFD)
        {
            perror("error accept failed");
            close(SocketFD);
            exit(EXIT_FAILURE);
        }

        thread(readSocketThread, ClientFD).detach();
    }

    close(SocketFD);
    return 0;
}
