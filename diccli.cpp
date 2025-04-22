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
#include <algorithm>
#include <fstream>
#include <vector>
#include <cmath>
#include <map>

using namespace std;

string nickname;
map<string, int> mapSockets;
int tablero[9] = {0,0,0,0,0,0,0,0,0}; // Tablero de Tic Tac Toe
int jugadas = 0;
int turno = 0;
pair<string, string> players;

int calcularHash(const char *datos, long tamano) {
    int hash = 0;
    for (long i = 0; i < tamano; i++) {
        hash = (hash + (unsigned char)datos[i]) % 100000;
    }
    return hash;
}

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

string readFile(const string& filePath) {
    ifstream file(filePath, ios::binary | ios::ate);
    if (!file.is_open()) {
        cerr << "Error al abrir el archivo\n";
        return "";
    }

    size_t fileSize = file.tellg();
    file.seekg(0, ios::beg);

    vector<char> buffer(fileSize);
    if (!file.read(buffer.data(), fileSize)) {
        cerr << "Error al leer el archivo\n";
        return "";
    }

    return string(buffer.data(), buffer.size());
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

void N_funcion(int socketFD) {
    char buffer[256];
    printf("Nickname: ");
    fgets(buffer, 100, stdin);
    buffer[strcspn(buffer, "\n")] = '\0';
    nickname = buffer;
    
    int message_size = nickname.length() + 1;
    sprintf(buffer, "%05dN%s", message_size, nickname.c_str());
    write(socketFD, buffer, 5 + message_size);
}

void L_funcion(int socketFD) {
    char buffer[10];
    sprintf(buffer, "%05dL", 1);
    write(socketFD, buffer, 6);
}

void M_funcion(int socketFD) {
    char recipient[100], message[900];
    printf("Destinatario: ");
    fgets(recipient, 100, stdin);
    recipient[strcspn(recipient, "\n")] = '\0';
    
    printf("Mensaje: ");
    fgets(message, 900, stdin);
    message[strcspn(message, "\n")] = '\0';
    
    char buffer[1024];
    int recipient_size = strlen(recipient);
    int total_size = 1 + 5 + recipient_size + strlen(message);
    sprintf(buffer, "%05dM%05d%s%s", total_size, recipient_size, recipient, message);
    write(socketFD, buffer, 5 + total_size);
}

void B_funcion(int socketFD) {
    char message[900];
    printf("Mensaje para todos: ");
    fgets(message, 900, stdin);
    message[strcspn(message, "\n")] = '\0';
    
    int msg_size = strlen(message);
    char buffer[1024];
    sprintf(buffer, "%05dB%05d%s", 1 + 5 + msg_size, msg_size, message);
    write(socketFD, buffer, 5 + 1 + 5 + msg_size);
}

void P_funcion(int socketFD) {
    char position[2];
    printf("Posición (0-8): ");
    fgets(position, 2, stdin);
    getchar(); // Consume el newline
    
    char buffer[10];
    sprintf(buffer, "%05dP%s", 2, position);
    write(socketFD, buffer, 7);
}

void F_funcion(int socketFD) {
    char destination[100], filepath[256];
    
    printf("Destinatario: ");
    fgets(destination, 100, stdin);
    destination[strcspn(destination, "\n")] = '\0';
    
    printf("Ruta del archivo: ");
    fgets(filepath, 256, stdin);
    filepath[strcspn(filepath, "\n")] = '\0';

    string fullpath(filepath);
    size_t last_slash = fullpath.find_last_of("/\\");
    string filename = (last_slash == string::npos) ? fullpath : fullpath.substr(last_slash + 1);
    
    ifstream file(fullpath, ios::binary | ios::ate);
    if (!file.is_open()) {
        cerr << "Error al abrir el archivo\n";
        return;
    }
    
    long file_size = file.tellg();
    file.seekg(0, ios::beg);
    
    vector<char> file_content(file_size);
    if (!file.read(file_content.data(), file_size)) {
        cerr << "Error al leer el archivo\n";
        return;
    }
    file.close();
    
    int hash_value = calcularHash(file_content.data(), file_size);
    char hash_str[6];
    sprintf(hash_str, "%05d", hash_value);
    
    long tamanoDestino = strlen(destination);
    long tamanoNombreArchivo = filename.size();
    
    long dataLen = 1 + 5 + tamanoDestino + 100 + tamanoNombreArchivo + 18 + file_size + 5;
    long total_size = 5 + dataLen;
    char *buffer = new char[total_size];
    memset(buffer, 0, total_size);
    
    sprintf(buffer, "%05ld", dataLen);
    int pos = 5;
    
    buffer[pos++] = 'F';
    
    {
        char tmp[6];
        sprintf(tmp, "%05ld", tamanoDestino);
        memcpy(buffer + pos, tmp, 5);
        pos += 5;
    }
    
    memcpy(buffer + pos, destination, tamanoDestino);
    pos += tamanoDestino;
    
    {
        char tmp[101];
        sprintf(tmp, "%0100ld", tamanoNombreArchivo);
        memcpy(buffer + pos, tmp, 100);
        pos += 100;
    }
    
    memcpy(buffer + pos, filename.c_str(), tamanoNombreArchivo);
    pos += tamanoNombreArchivo;
    
    {
        char tmp[19];
        sprintf(tmp, "%018ld", file_size);
        memcpy(buffer + pos, tmp, 18);
        pos += 18;
    }
    
    memcpy(buffer + pos, file_content.data(), file_size);
    pos += file_size;
    
    memcpy(buffer + pos, hash_str, 5);
    pos += 5;
    
    write(socketFD, buffer, total_size);
    printf("Archivo enviado.\n");
    delete[] buffer;
}

void Q_funcion(int socketFD) {
    char buffer[10];
    sprintf(buffer, "%05dQ", 1);
    write(socketFD, buffer, 6);
}

void showMenu() {
    printf("\nOpciones:\n");
    printf("L. Listar usuarios\n");
    printf("M. Enviar mensaje\n");
    printf("B. Enviar mensaje a todos\n");
    printf("P. Jugar Tic Tac Toe (posición 0-8)\n");
    printf("F. Enviar archivo\n");
    printf("Q. Salir\n");
    printf("Seleccione opción: ");
    fflush(stdout);
}

void readSocketThread(int cliSocket) {
    char buffer[10240];
    int n;
    
    while(true) {
        n = read(cliSocket, buffer, 5);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        int message_size = atoi(buffer);
        
        n = read(cliSocket, buffer, 1);
        if (n <= 0) break;
        
        char message_type = buffer[0];
        
        if (message_type == 'l') {
            n = read(cliSocket, buffer, message_size - 1);
            if (n <= 0) break;
            buffer[n] = '\0';
            printf("\nUsuarios: %s\n", buffer);
        }
        else if (message_type == 'm') {
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            buffer[n] = '\0';
            int sender_size = atoi(buffer);
            
            char sender[100];
            n = read(cliSocket, sender, sender_size);
            if (n <= 0) break;
            sender[n] = '\0';
            
            char msg[900];
            n = read(cliSocket, msg, message_size - 1 - 5 - sender_size);
            if (n <= 0) break;
            msg[n] = '\0';
            printf("\nMensaje de %s: %s\n", sender, msg);
        }
        else if (message_type == 'b') {
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            buffer[n] = '\0';
            int msg_size = atoi(buffer);
            
            char msg[900];
            n = read(cliSocket, msg, msg_size);
            if (n <= 0) break;
            msg[n] = '\0';
            
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            buffer[n] = '\0';
            int nick_size = atoi(buffer);
            
            char sender[100];
            n = read(cliSocket, sender, nick_size);
            if (n <= 0) break;
            sender[n] = '\0';
            
            printf("\nMensaje para todos de %s: %s\n", sender, msg);
        }
        else if (message_type == 'p') {
            n = read(cliSocket, buffer, 9);
            if (n <= 0) break;
            buffer[n] = '\0';
            string tablero_str(buffer);
            
            n = read(cliSocket, buffer, 2);
            if (n <= 0) break;
            buffer[n] = '\0';
            int contra_size = atoi(buffer);
            
            n = read(cliSocket, buffer, contra_size);
            if (n <= 0) break;
            buffer[n] = '\0';
            string contra(buffer);
            
            n = read(cliSocket, buffer, 1);
            if (n <= 0) break;
            buffer[n] = '\0';
            string estado(buffer);
            
            printf("\n--- Tic Tac Toe ---\n");
            printf("Contra: %s\n", contra.c_str());
            printf(" %c | %c | %c \n", tablero_str[0], tablero_str[1], tablero_str[2]);
            printf("---+---+---\n");
            printf(" %c | %c | %c \n", tablero_str[3], tablero_str[4], tablero_str[5]);
            printf("---+---+---\n");
            printf(" %c | %c | %c \n", tablero_str[6], tablero_str[7], tablero_str[8]);
            
            if(estado == "6") printf("\n¡Ganaste!\n");
            else if(estado == "5") printf("\nPerdiste contra %s\n", contra.c_str());
            else if(estado == "8") printf("\nEmpate contra %s\n", contra.c_str());
        }
        else if (message_type == 'f') {
            char sender[100];
            char filename[256];
            char hash_recibido[6];
            
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            buffer[n] = '\0';
            int sender_size = atoi(buffer);
            
            n = read(cliSocket, sender, sender_size);
            if (n <= 0) break;
            sender[n] = '\0';
            
            n = read(cliSocket, buffer, 100);
            if (n <= 0) break;
            buffer[100] = '\0';
            long filename_size = strtol(buffer, NULL, 10);
            
            n = read(cliSocket, filename, filename_size);
            if (n <= 0) break;
            filename[n] = '\0';
            
            n = read(cliSocket, buffer, 18);
            if (n <= 0) break;
            buffer[18] = '\0';
            long file_size = atol(buffer);
            
            vector<char> file_content(file_size);
            long total_read = 0;
            while (total_read < file_size) {
                n = read(cliSocket, file_content.data() + total_read, file_size - total_read);
                if (n <= 0) break;
                total_read += n;
            }
            
            n = read(cliSocket, hash_recibido, 5);
            if (n <= 0) break;
            hash_recibido[5] = '\0';
            
            int hash_calculado = calcularHash(file_content.data(), file_size);
            char hash_str[6];
            sprintf(hash_str, "%05d", hash_calculado);
            
            if (strcmp(hash_str, hash_recibido) == 0) {
                printf("\nArchivo recibido de %s: %s (%ld bytes, hash OK)\n", 
                      sender, filename, file_size);
                
                string new_filename = string(filename) + "_recibido";
                ofstream outfile(new_filename, ios::binary);

                if (outfile.is_open()) {
                    outfile.write(file_content.data(), file_size);
                    outfile.close();
                    printf("Archivo guardado como: %s\n", filename);
                } else {
                    printf("Error al guardar el archivo\n");
                }
            } else {
                printf("\nArchivo recibido de %s con hash incorrecto (esperado: %s, recibido: %s)\n",
                      sender, hash_str, hash_recibido);
            }
        }
        
        showMenu();
    }
    
    printf("\nDesconectado del servidor.\n");
    close(cliSocket);
    exit(0);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[1024];

    if (-1 == SocketFD) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45002);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("Connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    N_funcion(SocketFD);
    std::thread(readSocketThread, SocketFD).detach();

    char option;
    do {
        showMenu();
        scanf(" %c", &option);
        getchar();
        
        switch(option) {
            case 'L':
            case 'l':
                L_funcion(SocketFD);
                break;
                
            case 'M':
            case 'm':
                M_funcion(SocketFD);
                break;
                
            case 'B':
            case 'b':
                B_funcion(SocketFD);
                break;
                
            case 'P':
            case 'p':
                P_funcion(SocketFD);
                break;
                
            case 'F':
            case 'f':
                F_funcion(SocketFD);
                break;
                
            case 'Q':
            case 'q':
                Q_funcion(SocketFD);
                printf("Desconectando...\n");
                break;
                
            default:
                printf("Opción inválida\n");
        }
    } while (option != 'Q' && option != 'q');

    close(SocketFD);
    return 0;
}
