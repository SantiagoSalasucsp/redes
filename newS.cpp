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
#include <list>

using namespace std;

struct Cliente {
    int socket;
    string nombre;
};

list<Cliente> clientes;

void manejadorCliente(int socketCliente) {
    char buffer[256];
    while (true) {
        int n = read(socketCliente, buffer, 256);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        string mensaje(buffer);
        size_t separador = mensaje.find(':');
        
        if (separador != string::npos) {
            string destino = mensaje.substr(0, separador);
            string texto = mensaje.substr(separador + 1);
            
            for (auto& cliente : clientes) {
                if (cliente.nombre == destino) {
                    write(cliente.socket, texto.c_str(), texto.size());
                    break;
                }
            }
        }
    }
    
    // Eliminar cliente desconectado
    for (auto it = clientes.begin(); it != clientes.end(); ++it) {
        if (it->socket == socketCliente) {
            cout << "Cliente desconectado: " << it->nombre << endl;
            clientes.erase(it);
            break;
        }
    }
    close(socketCliente);
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        perror("Error al crear socket");
        exit(1);
    }

    sockaddr_in direccion;
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(45000);
    direccion.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (sockaddr*)&direccion, sizeof(direccion)) {
        perror("Error en bind");
        exit(1);
    }

    if (listen(serverSocket, 10)) {
        perror("Error en listen");
        exit(1);
    }

    cout << "Servidor iniciado. Esperando conexiones..." << endl;

    while (true) {
        int socketCliente = accept(serverSocket, NULL, NULL);
        if (socketCliente == -1) {
            perror("Error en accept");
            continue;
        }

        char nombre[50];
        int n = read(socketCliente, nombre, 50);
        nombre[n] = '\0';

        Cliente nuevo;
        nuevo.socket = socketCliente;
        nuevo.nombre = nombre;
        clientes.push_back(nuevo);

        cout << "Nuevo cliente conectado: " << nombre << endl;
        thread(manejadorCliente, socketCliente).detach();
    }

    close(serverSocket);
    return 0;
}
