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
int tablero[9] = {0,0,0,0,0,0,0,0,0};
int jugadas = 0;
int turno = 0;
pair<string, string> players;

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

string tablero_string() {
    string ret;
    for(int i = 0; i < 9; i++) {
        ret += to_string(tablero[i]);
    }
    return ret;
}

int victoria(int jugador, int turno_actual) {
    if(tablero[0] == jugador && tablero[1] == jugador && tablero[2] == jugador) return 5;
    else if(tablero[3] == jugador && tablero[4] == jugador && tablero[5] == jugador) return 5;
    else if(tablero[6] == jugador && tablero[7] == jugador && tablero[8] == jugador) return 5;
    else if(tablero[0] == jugador && tablero[3] == jugador && tablero[6] == jugador) return 5;
    else if(tablero[1] == jugador && tablero[4] == jugador && tablero[7] == jugador) return 5;
    else if(tablero[2] == jugador && tablero[5] == jugador && tablero[8] == jugador) return 5;
    else if(tablero[0] == jugador && tablero[4] == jugador && tablero[8] == jugador) return 5;
    else if(tablero[2] == jugador && tablero[4] == jugador && tablero[6] == jugador) return 5;
    return turno_actual;
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

void P_funcion(int socketFD, const string& nickname) {
    char buffer[10];
    int n = read(socketFD, buffer, 1);
    if (n <= 0) return;
    
    int pos = atoi(buffer);
    
    if(players.first.empty() || players.first == nickname) {
        players.first = nickname;
    } 
    else if(players.second.empty() || players.second == nickname) {
        players.second = nickname;
    }
    
    if(players.first == nickname && turno == 0) {
        if(tablero[pos] == 0) {
            tablero[pos] = 1;
            jugadas++;
            turno = victoria(1, turno);
            if(turno != 5) turno = 1;
            if(jugadas == 8) turno = 8;
            
            string protocolo = "p" + tablero_string();
            string proto2 = protocolo;
            
            proto2 += ceros(2, players.first) + players.first + to_string(turno);
            protocolo += ceros(2, players.second) + players.second + to_string(turno + 1);
            
            write(socketFD, protocolo.c_str(), protocolo.length());
            
            if(mapSockets.find(players.second) != mapSockets.end()) {
                write(mapSockets[players.second], proto2.c_str(), proto2.length());
            }
        }
    }
    else if(players.second == nickname && turno == 1) {
        if(tablero[pos] == 0) {
            tablero[pos] = 2;
            jugadas++;
            turno = victoria(2, turno);
            if(turno != 5) turno = 0;
            if(jugadas == 8) turno = 8;
            
            string protocolo = "p" + tablero_string();
            string proto2 = protocolo;
            
            proto2 += ceros(2, players.second) + players.second + to_string(turno);
            protocolo += ceros(2, players.first) + players.first + to_string(turno + 1);
            
            write(socketFD, protocolo.c_str(), protocolo.length());
            
            if(mapSockets.find(players.first) != mapSockets.end()) {
                write(mapSockets[players.first], proto2.c_str(), proto2.length());
            }
        }
    }
    
    if(turno == 5 || jugadas == 8) {
        for(int i = 0; i < 9; i++) tablero[i] = 0;
        turno = 0;
        players = make_pair("", "");
        jugadas = 0;
    }
}

void F_funcion(int socketFD, const string& sender) {
    char buffer[20480];
    int n;
    
    n = read(socketFD, buffer, 5);
    if (n <= 0) return;
    buffer[n] = '\0';
    int tamanoDest = atoi(buffer);

    char nombreDestino[256];
    n = read(socketFD, nombreDestino, tamanoDest);
    nombreDestino[n] = '\0';

    n = read(socketFD, buffer, 100);
    buffer[100] = '\0';
    long tamanoNombreArchivo = strtol(buffer, NULL, 10);

    char *nombreArchivoC = new char[tamanoNombreArchivo + 1];
    n = read(socketFD, nombreArchivoC, tamanoNombreArchivo);
    nombreArchivoC[n] = '\0';
    string nombreArchivo = string(nombreArchivoC, tamanoNombreArchivo);
    delete[] nombreArchivoC;

    n = read(socketFD, buffer, 18);
    buffer[18] = '\0';
    long tamanoArchivo = atol(buffer);

    char *datosArchivo = new char[tamanoArchivo];
    long totalLeido = 0;
    while (totalLeido < tamanoArchivo) {
        n = read(socketFD, datosArchivo + totalLeido, tamanoArchivo - totalLeido);
        if (n <= 0) break;
        totalLeido += n;
    }

    char hashCampo[6];
    n = read(socketFD, hashCampo, 5);
    hashCampo[5] = '\0';

    int tamanoEmisor = sender.size();
    long dataLen_f = 1 + 5 + tamanoEmisor + 100 + tamanoNombreArchivo + 18 + tamanoArchivo + 5;
    long totalMensaje_f = 5 + dataLen_f;
    char *bufferEnvio = new char[totalMensaje_f];
    memset(bufferEnvio, 0, totalMensaje_f);

    sprintf(bufferEnvio, "%05ld", dataLen_f);
    int posEnvio = 5;
    
    bufferEnvio[posEnvio++] = 'f';

    {
        char tmp[6];
        sprintf(tmp, "%05d", tamanoEmisor);
        memcpy(bufferEnvio + posEnvio, tmp, 5);
        posEnvio += 5;
    }

    memcpy(bufferEnvio + posEnvio, sender.c_str(), tamanoEmisor);
    posEnvio += tamanoEmisor;

    {
        char tmp[101];
        sprintf(tmp, "%0100ld", tamanoNombreArchivo);
        memcpy(bufferEnvio + posEnvio, tmp, 100);
        posEnvio += 100;
    }

    memcpy(bufferEnvio + posEnvio, nombreArchivo.c_str(), tamanoNombreArchivo);
    posEnvio += tamanoNombreArchivo;

    {
        char tmp[19];
        sprintf(tmp, "%018ld", tamanoArchivo);
        memcpy(bufferEnvio + posEnvio, tmp, 18);
        posEnvio += 18;
    }

    memcpy(bufferEnvio + posEnvio, datosArchivo, tamanoArchivo);
    posEnvio += tamanoArchivo;

    memcpy(bufferEnvio + posEnvio, hashCampo, 5);
    posEnvio += 5;

    if (mapSockets.find(nombreDestino) != mapSockets.end()) {
        write(mapSockets[nombreDestino], bufferEnvio, totalMensaje_f);
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
            
            B_funcion(nickname, message);
        }
        else if (buffer[0] == 'P') {
            P_funcion(cliSocket, nickname);
        }
        else if (buffer[0] == 'F') {
            F_funcion(cliSocket, nickname);
        }
        else if (buffer[0] == 'Q') {
            break;
        }
    } while (true);
    
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
