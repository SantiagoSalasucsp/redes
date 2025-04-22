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
#include <string>
#include <algorithm>
#include <fstream>
#include <vector>
#include <cmath>

using namespace std;

map<string, int> mapSockets;

int contarCifras(int numero) {
    if (numero == 0) return 1;
    return log10(abs(numero)) + 1;
}

string ceros(int tam, string num) {
    string ret;
    if (contarCifras(num.length()) < tam) {
        int total = tam - contarCifras(num.length());
        for (int i = 0; i < total; i++) ret += "0";
    }
    return ret + to_string(num.length());
}

string ceros_int(int tam, int num) {
    string ret;
    if (contarCifras(num) < tam) {
        int total = tam - contarCifras(num);
        for (int i = 0; i < total; i++) ret += "0";
    }
    return ret + to_string(num);
}

void L_funcion(int socket, const string& nickname) {
    string msg;
    for (const auto& pair : mapSockets) {
        if (!msg.empty()) msg += ",";
        msg += pair.first;
    }
    
    char buffer[1024];
    sprintf(buffer, "%05dl%s", (int)msg.length()+1, msg.c_str());
    write(mapSockets[nickname], buffer, 5 + 1 + msg.length());
}

void M_funcion(const string& sender, const string& recipient, const string& message) {
    char buffer[1024];
    int msg_size = message.length();
    int name_size = sender.length();
    int total_size = 1 + 5 + name_size + msg_size;
    
    sprintf(buffer, "%05dm%05d%s%s", total_size, name_size, sender.c_str(), message.c_str());
    
    if (mapSockets.find(recipient) != mapSockets.end()) {
        write(mapSockets[recipient], buffer, total_size + 5);
    }
}

void B_funcion(const string& sender, const string& message) {
    int msg_size = message.length();
    int name_size = sender.length();
    int total_size = 1 + 5 + msg_size + 5 + name_size;
    
    char buffer[1024];
    sprintf(buffer, "%05db%05d%s%05d%s", total_size, msg_size, message.c_str(), name_size, sender.c_str());
    
    for (const auto& pair : mapSockets) {
        if (pair.first != sender) {
            write(pair.second, buffer, total_size + 5);
        }
    }
}

void F_funcion(int socketFD, const string& sender) {
    char buffer[20480];
    int n;
    
    // 5B tamaño destinatario
    n = read(socketFD, buffer, 5);
    if (n <= 0) return;
    buffer[n] = '\0';
    int tamanoDest = atoi(buffer);

    char nombreDestino[256];
    n = read(socketFD, nombreDestino, tamanoDest);
    nombreDestino[n] = '\0';

    // 100B tamaño nombre archivo
    n = read(socketFD, buffer, 100);
    buffer[100] = '\0';
    long tamanoNombreArchivo = strtol(buffer, NULL, 10);

    // nombre del archivo
    char *nombreArchivoC = new char[tamanoNombreArchivo + 1];
    n = read(socketFD, nombreArchivoC, tamanoNombreArchivo);
    nombreArchivoC[n] = '\0';
    std::string nombreArchivo = std::string(nombreArchivoC, tamanoNombreArchivo);
    delete[] nombreArchivoC;

    // 18B tamaño del archivo
    n = read(socketFD, buffer, 18);
    buffer[18] = '\0';
    long tamanoArchivo = atol(buffer);

    // contenido del archivo
    char *datosArchivo = new char[tamanoArchivo];
    long totalLeido = 0;
    while (totalLeido < tamanoArchivo) {
        n = read(socketFD, datosArchivo + totalLeido, tamanoArchivo - totalLeido);
        if (n <= 0) break;
        totalLeido += n;
    }

    // 5B hash
    char hashCampo[6];
    n = read(socketFD, hashCampo, 5);
    hashCampo[5] = '\0';

    // envio de mensaje f
    int tamanoEmisor = sender.size();
    long dataLen_f = 1                     // 1B tipo 'f'
                     + 5                   // 5B tamaño emisor
                     + tamanoEmisor        // nickname emisor (sender)
                     + 100                 // 100B tamaño nombre del archivo
                     + tamanoNombreArchivo // nombre del archivo
                     + 18                  // 18B tamaño del archivo
                     + tamanoArchivo       // contenido del archivo
                     + 5;                  // 5B hash

    long totalMensaje_f = 5 + dataLen_f;
    char *bufferEnvio = new char[totalMensaje_f];
    memset(bufferEnvio, 0, totalMensaje_f);

    // escribir 5B dataLen_f
    sprintf(bufferEnvio, "%05ld", dataLen_f);

    int posEnvio = 5;
    // escribir 1B tipo f
    bufferEnvio[posEnvio++] = 'f';

    // escribir 5B tamaño emisor
    {
        char tmp[6];
        sprintf(tmp, "%05d", tamanoEmisor);
        memcpy(bufferEnvio + posEnvio, tmp, 5);
        posEnvio += 5;
    }

    // escribir nickname emisor
    memcpy(bufferEnvio + posEnvio, sender.c_str(), tamanoEmisor);
    posEnvio += tamanoEmisor;

    // escribir 100B tamaño nombre del archivo
    {
        char tmp[101];
        sprintf(tmp, "%0100ld", tamanoNombreArchivo);
        memcpy(bufferEnvio + posEnvio, tmp, 100);
        posEnvio += 100;
    }

    // escribir nombre del archivo
    memcpy(bufferEnvio + posEnvio, nombreArchivo.c_str(), tamanoNombreArchivo);
    posEnvio += tamanoNombreArchivo;

    // escribir 18B tamaño archivo
    {
        char tmp[19];
        sprintf(tmp, "%018ld", tamanoArchivo);
        memcpy(bufferEnvio + posEnvio, tmp, 18);
        posEnvio += 18;
    }

    // escribir contenido del archivo
    memcpy(bufferEnvio + posEnvio, datosArchivo, tamanoArchivo);
    posEnvio += tamanoArchivo;

    // escribir 5B hash
    memcpy(bufferEnvio + posEnvio, hashCampo, 5);
    posEnvio += 5;

    // enviar al destinatario
    if (mapSockets.find(nombreDestino) != mapSockets.end()) {
        int socketDestino = mapSockets[nombreDestino];
        write(socketDestino, bufferEnvio, totalMensaje_f);
        printf("[Servidor] Reenviando archivo de %s a %s\n", sender.c_str(), nombreDestino);
    } else {
        printf("[Servidor] Destinatario %s no encontrado\n", nombreDestino);
    }
    delete[] bufferEnvio;
    delete[] datosArchivo;
}

void readSocketThread(int cliSocket, string nickname) {
    char buffer[20480];
    int n, total_size;
    
    do {
        n = read(cliSocket, buffer, 5);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        total_size = atoi(buffer);
        
        n = read(cliSocket, buffer, 1);
        if (n <= 0) break;
        
        if (buffer[0] == 'L') {
            L_funcion(cliSocket, nickname);
        }
        else if (buffer[0] == 'M') {
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            buffer[n] = '\0';
            int recipient_size = atoi(buffer);
            
            n = read(cliSocket, buffer, recipient_size);
            if (n <= 0) break;
            buffer[n] = '\0';
            string recipient(buffer);
            
            n = read(cliSocket, buffer, total_size - 1 - 5 - recipient_size);
            if (n <= 0) break;
            buffer[n] = '\0';
            string message(buffer);
            
            if (mapSockets.find(recipient) != mapSockets.end()) {
                M_funcion(nickname, recipient, message);
                printf("Message from %s to %s: %s\n", nickname.c_str(), recipient.c_str(), message.c_str());
            } else {
                printf("Recipient %s not found\n", recipient.c_str());
            }
        }
        else if (buffer[0] == 'B') {
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            buffer[n] = '\0';
            int msg_size = atoi(buffer);
            
            char message[1024];
            n = read(cliSocket, message, msg_size);
            if (n <= 0) break;
            message[n] = '\0';
            
            printf("Broadcast from %s: %s\n", nickname.c_str(), message);
            B_funcion(nickname, message);
        }
        else if (buffer[0] == 'F') {
            printf("File transfer initiated from %s\n", nickname.c_str());
            F_funcion(cliSocket, nickname);
        }
        else if (buffer[0] == 'Q') {
            printf("Client %s requested disconnect\n", nickname.c_str());
            break;
        }
    } while (true);
    
    printf("Client %s disconnected\n", nickname.c_str());
    close(cliSocket);
    mapSockets.erase(nickname);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    int port = 45002;
    
    if (-1 == SocketFD) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(port);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("Bind failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (-1 == listen(SocketFD, 10)) {
        perror("Listen failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    printf("Server initialized on port %d\n", port);

    while (true) {
        int ClientFD = accept(SocketFD, NULL, NULL);
        if (ClientFD < 0) {
            perror("Accept failed");
            continue;
        }

        char buffer[256];
        int n = read(ClientFD, buffer, 5);
        if (n <= 0) {
            close(ClientFD);
            continue;
        }
        
        buffer[n] = '\0';
        int size = atoi(buffer);
        
        n = read(ClientFD, buffer, 1);
        if (n <= 0 || buffer[0] != 'N') {
            close(ClientFD);
            continue;
        }
        
        n = read(ClientFD, buffer, size - 1);
        if (n <= 0) {
            close(ClientFD);
            continue;
        }
        
        buffer[n] = '\0';
        string nickname(buffer);
        
        mapSockets[nickname] = ClientFD;
        printf("New client: %s\n", nickname.c_str());
        
        thread(readSocketThread, ClientFD, nickname).detach();
    }

    close(SocketFD);
    return 0;
}