/* Server code in C */
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

using namespace std;

std::map<string, int> mapSockets;

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
    // Formato: [5B tamaño total][b][5B tamaño mensaje][mensaje][5B tamaño nickname][nickname]
    int msg_size = message.length();
    int name_size = sender.length();
    int total_size = 1 + 5 + msg_size + 5 + name_size;
    
    char buffer[1024];
    sprintf(buffer, "%05db%05d%s%05d%s", total_size, msg_size, message.c_str(), name_size, sender.c_str());
    
    // Send to all clients except the sender
    for (const auto& pair : mapSockets) {
        if (pair.first != sender) {
            write(pair.second, buffer, total_size + 5);
        }
    }
}

// Función para manejar la recepción de archivos
void F_funcion(int socketFD, const string& sender) {
    char buffer[1024];
    int n;
    
    // Leer tamaño del destino
    n = read(socketFD, buffer, 5);
    if (n <= 0) return;
    buffer[n] = '\0';
    int dest_size = atoi(buffer);
    
    // Leer destino
    char destination[20];
    n = read(socketFD, destination, dest_size);
    if (n <= 0) return;
    destination[n] = '\0';
    
    // Leer nombre del archivo (100B)
    char filename[101];
    n = read(socketFD, filename, 100);
    if (n <= 0) return;
    filename[n] = '\0';
    
    // Asegurar que el nombre del archivo termina en NULL
    for (int i = 0; i < 100; i++) {
        if (filename[i] == 0) break;
        if (i == 99) filename[i] = 0;
    }
    
    // Crear path para guardar el archivo temporalmente
    char temp_path[120];
    sprintf(temp_path, "server_temp_%s", filename);
    
    // Crear el archivo
    FILE* fp = fopen(temp_path, "wb");
    if (!fp) {
        printf("Error creating temporary file %s\n", temp_path);
        return;
    }
    
    // Leer el tamaño del archivo
    n = read(socketFD, buffer, 5);
    if (n <= 0) {
        fclose(fp);
        return;
    }
    buffer[n] = '\0';
    int filesize = atoi(buffer);
    
    // Leer el contenido del archivo
    int bytes_read = 0;
    while (bytes_read < filesize) {
        n = read(socketFD, buffer, min(1024, filesize - bytes_read));
        if (n <= 0) break;
        fwrite(buffer, 1, n, fp);
        bytes_read += n;
    }
    
    // Leer el hash
    n = read(socketFD, buffer, 5);
    if (n <= 0) {
        fclose(fp);
        return;
    }
    buffer[n] = '\0';
    int hash = atoi(buffer);
    
    fclose(fp);
    printf("File %s received from %s to %s, size: %d bytes\n", 
           filename, sender.c_str(), destination, filesize);
    
    // Si hay un destinatario válido, reenviar el archivo
    if (strcmp(destination, "all") != 0 && mapSockets.find(destination) != mapSockets.end()) {
        FILE* fp = fopen(temp_path, "rb");
        if (!fp) return;
        
        // Obtener tamaño del archivo
        fseek(fp, 0, SEEK_END);
        filesize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        // Tamaño total del mensaje
        int total_size = 1 + 5 + sender.length() + 100 + 5 + filesize + 5;
        
        // Enviar encabezado
        sprintf(buffer, "%05dF%05d%s", total_size, (int)sender.length(), sender.c_str());
        write(mapSockets[destination], buffer, 5 + 1 + 5 + sender.length());
        
        // Enviar nombre del archivo
        write(mapSockets[destination], filename, strlen(filename));
        // Padding para completar 100B
        char padding[100] = {0};
        write(mapSockets[destination], padding, 100 - strlen(filename));
        
        // Enviar tamaño del archivo
        sprintf(buffer, "%05d", filesize);
        write(mapSockets[destination], buffer, 5);
        
        // Enviar contenido del archivo
        bytes_read = 0;
        while (bytes_read < filesize) {
            n = fread(buffer, 1, 1024, fp);
            if (n <= 0) break;
            write(mapSockets[destination], buffer, n);
            bytes_read += n;
        }
        
        // Enviar hash (un simple checksum)
        sprintf(buffer, "%05d", hash);
        write(mapSockets[destination], buffer, 5);
        
        fclose(fp);
        printf("File %s forwarded to %s\n", filename, destination);
    }
    
    // Limpieza del archivo temporal
    remove(temp_path);
}

void readSocketThread(int cliSocket, string nickname) {
    char buffer[1024];
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
            // Formato recibido: [5B tamaño total][B][5B tamaño mensaje][mensaje]
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