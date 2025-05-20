#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <iostream>
#include <thread>
#include <map>
#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace std;

// Estructuras de datos globales
mutex mapaMutex;
unordered_map<string, sockaddr_in> mapaAddr;
unordered_map<string, vector<string>> clientePkgs;

// Estructura para la partida de Tic-Tac-Toe
struct Partida {
    char tablero[9] = {'_', '_', '_', '_', '_', '_', '_', '_', '_'};
    string jugadorO;
    string jugadorX;
    vector<string> espectadores;
    char turno = 'O';
    bool activa = false;
} partida;

string jugadorEnEspera;
mutex partidaMutex;

// Prototipos de funciones
void procesarMensajes(vector<string> pkgs, string nickname, int sock, char tipo);
void enviarMensaje(int sock, const sockaddr_in &dest, const string &msg, const string &remitente = "servidor");
void enviarBroadcast(int sock, const string &msg, const string &remitente, const string &excluir = "");
void enviarTablero(int sock);
void enviarTurno(int sock, const string &nick, char simbolo);
void finalizarPartida(int sock, char resultado);
bool hayGanador(char s);
bool tableroLleno();
vector<string> partirMensaje(const string &msg, int tamMax);
int calcularHash(const char *datos, long tamano);

// Función principal para procesar los mensajes recibidos
void procesarMensajes(vector<string> pkgs, string nickname, int sock, char tipo) {
    // Mensaje de lista de usuarios (L)
    if (tipo == 'L') {
        lock_guard<mutex> lock(mapaMutex);
        string lista;
        string separador = "";
        
        for (const auto &par : mapaAddr) {
            if (par.first != nickname) {
                lista += separador + par.first;
                separador = ",";
            }
        }
        
        if (lista.empty()) {
            lista = "No hay otros usuarios conectados";
        }
        
        enviarMensaje(sock, mapaAddr[nickname], lista);
    }
    // Mensaje privado (M)
    else if (tipo == 'M') {
        try {
            int tamMsg = stoi(pkgs[0].substr(10, 5));
            int tamDest = stoi(pkgs[0].substr(15 + tamMsg, 5));
            string destino = pkgs[0].substr(15 + tamMsg + 5, tamDest);

            string mensaje;
            for (const auto &pkg : pkgs) {
                int t = stoi(pkg.substr(10, 5));
                mensaje += pkg.substr(15, t);
            }

            lock_guard<mutex> lock(mapaMutex);
            if (mapaAddr.find(destino) != mapaAddr.end()) {
                enviarMensaje(sock, mapaAddr[destino], mensaje, nickname);
            } else {
                enviarMensaje(sock, mapaAddr[nickname], "Usuario no encontrado: " + destino);
            }
        } catch (...) {
            enviarMensaje(sock, mapaAddr[nickname], "Error al procesar mensaje privado");
        }
    }
    // Broadcast (B)
    else if (tipo == 'B') {
        string mensaje;
        for (const auto &pkg : pkgs) {
            int t = stoi(pkg.substr(10, 5));
            mensaje += pkg.substr(15, t);
        }

        lock_guard<mutex> lock(mapaMutex);
        enviarBroadcast(sock, mensaje, nickname, nickname);
    }
    // Archivo (F)
    else if (tipo == 'F') {
        try {
            int tamDest = stoi(pkgs[0].substr(10, 5));
            string destino = pkgs[0].substr(15, tamDest);
            int tamFileName = stoi(pkgs[0].substr(15 + tamDest, 100));
            string fileName = pkgs[0].substr(15 + tamDest + 100, tamFileName);
            long tamFile = stol(pkgs[0].substr(15 + tamDest + 100 + tamFileName, 18));
            string hash = pkgs[0].substr(15 + tamDest + 100 + tamFileName + 18 + tamFile, 5);

            string archivo;
            for (const auto &pkg : pkgs) {
                long lenTrozo = stol(pkg.substr(15 + tamDest + 100 + tamFileName, 18));
                size_t posDatos = 15 + tamDest + 100 + tamFileName + 18;
                archivo += pkg.substr(posDatos, lenTrozo);
            }

            lock_guard<mutex> lock(mapaMutex);
            if (mapaAddr.find(destino) != mapaAddr.end()) {
                // Construir paquete de archivo
                char buffer[500];
                memset(buffer, '#', 500);
                
                // Un solo paquete (simplificado)
                memcpy(buffer, "0001", 4);
                int totalSize = 1 + 5 + nickname.length() + 100 + fileName.length() + 18 + archivo.length() + 5;
                sprintf(buffer + 4, "%05d", totalSize);
                
                int offset = 9;
                buffer[offset++] = 'f';
                
                // Remitente
                sprintf(buffer + offset, "%05d", (int)nickname.length());
                offset += 5;
                memcpy(buffer + offset, nickname.c_str(), nickname.length());
                offset += nickname.length();
                
                // Nombre archivo
                sprintf(buffer + offset, "%0100d", (int)fileName.length());
                offset += 100;
                memcpy(buffer + offset, fileName.c_str(), fileName.length());
                offset += fileName.length();
                
                // Datos archivo
                sprintf(buffer + offset, "%018ld", archivo.length());
                offset += 18;
                memcpy(buffer + offset, archivo.c_str(), archivo.length());
                offset += archivo.length();
                
                // Hash
                memcpy(buffer + offset, hash.c_str(), 5);
                
                // Enviar
                sendto(sock, buffer, 500, 0, 
                      (sockaddr*)&mapaAddr[destino], sizeof(sockaddr_in));
            } else {
                enviarMensaje(sock, mapaAddr[nickname], "Usuario no encontrado: " + destino);
            }
        } catch (...) {
            enviarMensaje(sock, mapaAddr[nickname], "Error al procesar archivo");
        }
    }
    // Salir (Q)
    else if (tipo == 'Q') {
        lock_guard<mutex> lock(mapaMutex);
        mapaAddr.erase(nickname);
        cout << "Cliente " << nickname << " ha salido\n";
    }
    // Unirse a Tic-Tac-Toe (J)
    else if (tipo == 'J') {
        lock_guard<mutex> lock(partidaMutex);
        
        if (!partida.activa && jugadorEnEspera.empty()) {
            jugadorEnEspera = nickname;
            enviarMensaje(sock, mapaAddr[nickname], "Esperando otro jugador...");
        } 
        else if (!partida.activa && !jugadorEnEspera.empty() && jugadorEnEspera != nickname) {
            partida.activa = true;
            partida.jugadorO = jugadorEnEspera;
            partida.jugadorX = nickname;
            jugadorEnEspera = "";
            
            enviarMensaje(sock, mapaAddr[partida.jugadorO], "¡Partida iniciada! Eres O");
            enviarMensaje(sock, mapaAddr[partida.jugadorX], "¡Partida iniciada! Eres X");
            
            enviarTablero(sock);
            enviarTurno(sock, partida.jugadorO, 'O');
        }
        else if (partida.activa) {
            enviarMensaje(sock, mapaAddr[nickname], "¿Quieres ver la partida? (y/n)");
        }
    }
    // Ver partida (V)
    else if (tipo == 'V') {
        lock_guard<mutex> lock(partidaMutex);
        
        if (partida.activa) {
            // Evitar duplicados
            if (find(partida.espectadores.begin(), partida.espectadores.end(), nickname) == partida.espectadores.end() &&
                nickname != partida.jugadorO && nickname != partida.jugadorX) {
                partida.espectadores.push_back(nickname);
            }
            enviarTablero(sock);
        }
    }
    // Movimiento en Tic-Tac-Toe (P)
    else if (tipo == 'P') {
        lock_guard<mutex> lock(partidaMutex);
        
        if (!partida.activa) {
            enviarMensaje(sock, mapaAddr[nickname], "No hay partida activa");
            return;
        }
        
        char pos = pkgs[0][10];
        char simb = pkgs[0][11];
        
        // Validar turno
        if ((simb == 'O' && nickname != partida.jugadorO) || 
            (simb == 'X' && nickname != partida.jugadorX)) {
            enviarMensaje(sock, mapaAddr[nickname], "No es tu turno");
            return;
        }
        
        if (partida.turno != simb) {
            enviarMensaje(sock, mapaAddr[nickname], "No es tu turno");
            return;
        }
        
        // Validar posición
        int idx = pos - '1';
        if (idx < 0 || idx > 8 || partida.tablero[idx] != '_') {
            enviarMensaje(sock, mapaAddr[nickname], "Posición inválida");
            enviarTurno(sock, nickname, simb); // Reenviar turno
            return;
        }
        
        // Hacer movimiento
        partida.tablero[idx] = simb;
        enviarTablero(sock);
        
        // Verificar fin del juego
        if (hayGanador(simb)) {
            finalizarPartida(sock, (simb == 'O') ? 'O' : 'X');
        } 
        else if (tableroLleno()) {
            finalizarPartida(sock, 'E');
        } 
        else {
            // Cambiar turno
            partida.turno = (partida.turno == 'O') ? 'X' : 'O';
            string proximo = (partida.turno == 'O') ? partida.jugadorO : partida.jugadorX;
            enviarTurno(sock, proximo, partida.turno);
        }
    }
}

// Función para enviar un mensaje simple
void enviarMensaje(int sock, const sockaddr_in &dest, const string &msg, const string &remitente) {
    char buffer[500];
    memset(buffer, '#', 500);
    
    // Un solo paquete
    memcpy(buffer, "0001", 4);
    
    int tamTot = 1 + 5 + msg.length() + 5 + remitente.length();
    sprintf(buffer + 4, "%05d", tamTot);
    
    int offset = 9;
    buffer[offset++] = 'm';
    
    // Tamaño mensaje
    sprintf(buffer + offset, "%05d", (int)msg.length());
    offset += 5;
    memcpy(buffer + offset, msg.c_str(), msg.length());
    offset += msg.length();
    
    // Tamaño remitente
    sprintf(buffer + offset, "%05d", (int)remitente.length());
    offset += 5;
    memcpy(buffer + offset, remitente.c_str(), remitente.length());
    
    sendto(sock, buffer, 500, 0, (const sockaddr*)&dest, sizeof(dest));
}

// Función para enviar un broadcast
void enviarBroadcast(int sock, const string &msg, const string &remitente, const string &excluir) {
    lock_guard<mutex> lock(mapaMutex);
    
    for (const auto &par : mapaAddr) {
        if (par.first != excluir) {
            enviarMensaje(sock, par.second, msg, remitente);
        }
    }
}

// Función para enviar el estado del tablero a todos los involucrados
void enviarTablero(int sock) {
    char buffer[500];
    memset(buffer, '#', 500);
    
    // Un solo paquete
    memcpy(buffer, "0001", 4);
    memcpy(buffer + 4, "00010", 5); // Tamaño total fijo para tablero
    
    buffer[9] = 'X';
    memcpy(buffer + 10, partida.tablero, 9);
    
    // Enviar a jugadores
    if (!partida.jugadorO.empty()) {
        sendto(sock, buffer, 500, 0, 
              (sockaddr*)&mapaAddr[partida.jugadorO], sizeof(sockaddr_in));
    }
    if (!partida.jugadorX.empty()) {
        sendto(sock, buffer, 500, 0, 
              (sockaddr*)&mapaAddr[partida.jugadorX], sizeof(sockaddr_in));
    }
    
    // Enviar a espectadores
    for (const auto &esp : partida.espectadores) {
        sendto(sock, buffer, 500, 0, 
              (sockaddr*)&mapaAddr[esp], sizeof(sockaddr_in));
    }
}

// Función para notificar turno
void enviarTurno(int sock, const string &nick, char simbolo) {
    char buffer[500];
    memset(buffer, '#', 500);
    
    // Un solo paquete
    memcpy(buffer, "0001", 4);
    memcpy(buffer + 4, "00002", 5); // Tamaño total fijo para turno
    
    buffer[9] = 'T';
    buffer[10] = simbolo;
    
    sendto(sock, buffer, 500, 0, 
          (sockaddr*)&mapaAddr[nick], sizeof(sockaddr_in));
}

// Función para finalizar partida
void finalizarPartida(int sock, char resultado) {
    char buffer[500];
    memset(buffer, '#', 500);
    
    // Un solo paquete
    memcpy(buffer, "0001", 4);
    memcpy(buffer + 4, "00002", 5);
    
    buffer[9] = 'O';
    buffer[10] = resultado;
    
    // Notificar a jugadores
    sendto(sock, buffer, 500, 0, 
          (sockaddr*)&mapaAddr[partida.jugadorO], sizeof(sockaddr_in));
    sendto(sock, buffer, 500, 0, 
          (sockaddr*)&mapaAddr[partida.jugadorX], sizeof(sockaddr_in));
    
    // Notificar a espectadores
    for (const auto &esp : partida.espectadores) {
        sendto(sock, buffer, 500, 0, 
              (sockaddr*)&mapaAddr[esp], sizeof(sockaddr_in));
    }
    
    // Resetear partida
    partida = Partida();
}

// Funciones auxiliares para Tic-Tac-Toe
bool linea(int a, int b, int c, char s) {
    return partida.tablero[a] == s && partida.tablero[b] == s && partida.tablero[c] == s;
}

bool hayGanador(char s) {
    return linea(0, 1, 2, s) || linea(3, 4, 5, s) || linea(6, 7, 8, s) ||
           linea(0, 3, 6, s) || linea(1, 4, 7, s) || linea(2, 5, 8, s) ||
           linea(0, 4, 8, s) || linea(2, 4, 6, s);
}

bool tableroLleno() {
    for (char c : partida.tablero) {
        if (c == '_') return false;
    }
    return true;
}

// Función principal
int main() {
    int sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[500];

    // Crear socket UDP
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Configurar dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), 0, 8);

    // Enlazar socket
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(1);
    }

    cout << "Servidor UDP iniciado en el puerto 5000\n";

    // Bucle principal del servidor
    while (true) {
        int n = recvfrom(sock, buffer, 500, 0, 
                        (struct sockaddr *)&client_addr, &addr_len);
        if (n != 500) {
            cout << "ERROR: Tamaño de mensaje incorrecto\n";
            continue;
        }

        // Crear clave única para el cliente (IP:puerto)
        char clave[32];
        snprintf(clave, sizeof(clave), "%s:%d", 
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Copiar datagrama completo
        string pkg(buffer, 500);

        // Registro de nuevo cliente (tipo 'N')
        if (pkg[9] == 'N') {
            try {
                int tamNick = stoi(pkg.substr(4, 5)) - 1;
                string nick = pkg.substr(10, tamNick);

                lock_guard<mutex> lock(mapaMutex);
                mapaAddr[nick] = client_addr;
                cout << "Cliente registrado: " << nick << " (" << clave << ")\n";
                
                // Responder con mensaje de bienvenida
                enviarMensaje(sock, client_addr, "Bienvenido al servidor de chat");
            } catch (...) {
                cout << "Error al procesar registro\n";
            }
            continue;
        }

        // Buscar nick del remitente
        string nick;
        {
            lock_guard<mutex> lock(mapaMutex);
            for (const auto &par : mapaAddr) {
                if (memcmp(&par.second, &client_addr, sizeof(client_addr)) == 0) {
                    nick = par.first;
                    break;
                }
            }
        }

        if (nick.empty()) {
            cout << "Mensaje de cliente no registrado\n";
            continue;
        }

        // Procesar secuencia de paquetes
        int seq = stoi(pkg.substr(0, 2));
        int tot = stoi(pkg.substr(2, 2));
        int idx = (seq == 0) ? tot - 1 : seq - 1;

        // Almacenar paquete
        auto &vc = clientePkgs[clave];
        if (vc.empty()) {
            vc.resize(tot);
        }

        if (idx < 0 || idx >= tot || !vc[idx].empty()) {
            continue; // Paquete inválido o duplicado
        }

        vc[idx] = move(pkg);

        // Verificar si tenemos todos los paquetes
        bool completo = true;
        for (const auto &p : vc) {
            if (p.empty()) {
                completo = false;
                break;
            }
        }

        if (completo) {
            char tipo = vc[0][9];
            vector<string> partes = move(vc);
            clientePkgs.erase(clave);

            // Procesar en un hilo separado
            thread(procesarMensajes, move(partes), nick, sock, tipo).detach();
        }
    }

    close(sock);
    return 0;
}