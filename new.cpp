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

void leerMensajes(int socket) {
    char buffer[256];
    while (true) {
        int n = read(socket, buffer, 256);
        if (n <= 0) {
            cout << "Desconectado del servidor" << endl;
            break;
        }
        buffer[n] = '\0';
        cout << "Mensaje recibido: " << buffer << endl;
    }
}

int main() {
    int socketCliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socketCliente == -1) {
        perror("Error al crear socket");
        exit(1);
    }

    sockaddr_in direccion;
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &direccion.sin_addr);

    if (connect(socketCliente, (sockaddr*)&direccion, sizeof(direccion))) {
        perror("Error al conectar");
        exit(1);
    }

    cout << "Ingresa tu nombre: ";
    char nombre[50];
    cin >> nombre;
    write(socketCliente, nombre, strlen(nombre));

    thread(leerMensajes, socketCliente).detach();

    char buffer[256];
    while (true) {
        cout << "Destinatario: ";
        char destinatario[50];
        cin >> destinatario;

        cout << "Mensaje: ";
        char mensaje[200];
        cin >> mensaje;

        sprintf(buffer, "%s:%s", destinatario, mensaje);
        write(socketCliente, buffer, strlen(buffer));
    }

    close(socketCliente);
    return 0;
}
