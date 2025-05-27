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
#include <unistd.h>

using namespace std;

std::unordered_map<std::string, sockaddr_in> mapaAddr;
std::unordered_map<std::string, std::vector<std::string>> clientePkgs;

std::vector<std::string> partir(std::string msg, int tamMax) {
    std::vector<std::string> partes;
    int len = msg.size();
    for (int i = 0; i < len; i += tamMax) {
        partes.push_back(msg.substr(i, tamMax));
    }
    return partes;
}

struct Partida {
    char tablero[9] = {'_', '_', '_', '_', '_', '_', '_', '_', '_'};
    string jugadorO;
    string jugadorX;
    vector<string> espectadores;
    char turno = 'O';
    bool activa = false;
} partida;

string jugadorEnEspera;

void enviarM(int sock, const sockaddr_in &dest, const std::string &msg) {
    const std::string remitente = "servidor";
    const int lenMsg = (int)msg.size();
    const int lenRem = (int)remitente.size();

    int tamTot = 1 + 5 + lenMsg + 5 + lenRem;
    char buffer[500];
    std::memset(buffer, '#', 500);

    // Formato 5-5-5
    std::memcpy(buffer, "00001", 5);      // Secuencia
    std::memcpy(buffer + 5, "00001", 5);  // Total
    std::sprintf(buffer + 10, "%05d", tamTot); // Tamaño
    buffer[15] = 'm';                     // Tipo
    std::sprintf(buffer + 16, "%05d", lenMsg);
    std::memcpy(buffer + 21, msg.c_str(), lenMsg);
    int offset = 21 + lenMsg;
    std::sprintf(buffer + offset, "%05d", lenRem);
    offset += 5;
    std::memcpy(buffer + offset, remitente.c_str(), lenRem);

    if (sendto(sock, buffer, 500, 0, (const sockaddr *)&dest, sizeof(dest)) == -1)
        perror("sendto");
}

void enviarX_aTodos(int sock) {
    char pkt[500];
    std::memset(pkt, '#', 500);
    // Cabecera 5-5-5
    std::memcpy(pkt, "00001", 5);      // Secuencia
    std::memcpy(pkt + 5, "00001", 5);  // Total
    std::memcpy(pkt + 10, "00010", 5); // Tamaño (1+9)
    pkt[15] = 'X';                     // Tipo
    std::memcpy(pkt + 16, partida.tablero, 9); // Datos

    // Enviar a jugadores
    for (auto nick : {partida.jugadorO, partida.jugadorX})
        if (!nick.empty())
            sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[nick], sizeof(sockaddr_in));

    // Enviar a espectadores
    for (auto &esp : partida.espectadores)
        sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[esp], sizeof(sockaddr_in));
}

void enviarT(int sock, const std::string &nick, char simbolo) {
    char pkt[500];
    std::memset(pkt, '#', 500);
    // Cabecera 5-5-5
    std::memcpy(pkt, "00001", 5);      // Secuencia
    std::memcpy(pkt + 5, "00001", 5);  // Total
    std::memcpy(pkt + 10, "00002", 5); // Tamaño (1+1)
    pkt[15] = 'T';                     // Tipo
    pkt[16] = simbolo;                 // Datos

    sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[nick], sizeof(sockaddr_in));
}

bool linea(int a, int b, int c, char s) {
    return partida.tablero[a] == s && partida.tablero[b] == s && partida.tablero[c] == s;
}

bool ganador(char s) {
    return linea(0, 1, 2, s) || linea(3, 4, 5, s) || linea(6, 7, 8, s) ||
           linea(0, 3, 6, s) || linea(1, 4, 7, s) || linea(2, 5, 8, s) ||
           linea(0, 4, 8, s) || linea(2, 4, 6, s);
}

bool tableroLleno() {
    for (char c : partida.tablero)
        if (c == '_') return false;
    return true;
}

void procesarMensajes(std::vector<std::string> pkgs, std::string nickname, int sock, char tipo) {
    if (tipo == 'L') {
        std::string mensaje;
        for (auto it = mapaAddr.cbegin(); it != mapaAddr.cend(); ++it) {
            if ((*it).first != nickname) {
                if (!mensaje.empty()) mensaje += ",";
                mensaje += (*it).first;
            }
        }
        sockaddr_in dest = mapaAddr[nickname];

        if (mensaje.length() > (500 - 21)) {
            std::vector<std::string> piezas = partir(mensaje, 500 - 21);
            int totalPkg = piezas.size();
            for (int i = 0; i < totalPkg; i++) {
                char buffer[500];
                std::memset(buffer, '#', 500);
                // Cabecera 5-5-5
                std::sprintf(buffer, "%05d", i+1);         // Secuencia
                std::sprintf(buffer + 5, "%05d", totalPkg); // Total
                std::sprintf(buffer + 10, "%05d", (int)piezas[i].length() + 1); // Tamaño
                buffer[15] = 'l';                          // Tipo
                std::sprintf(buffer + 16, "%05d", (int)piezas[i].length());
                std::memcpy(buffer + 21, piezas[i].c_str(), piezas[i].length());
                sendto(sock, buffer, 500, 0, (sockaddr *)&dest, sizeof(dest));
            }
        } else {
            char buffer[500];
            std::memset(buffer, '#', 500);
            // Cabecera 5-5-5
            std::memcpy(buffer, "00001", 5);      // Secuencia
            std::memcpy(buffer + 5, "00001", 5); // Total
            std::sprintf(buffer + 10, "%05d", (int)mensaje.length() + 1); // Tamaño
            buffer[15] = 'l';                     // Tipo
            std::sprintf(buffer + 16, "%05d", (int)mensaje.length());
            std::memcpy(buffer + 21, mensaje.c_str(), mensaje.length());
            sendto(sock, buffer, 500, 0, (sockaddr *)&dest, sizeof(dest));
        }
    }
    else if (tipo == 'M') {
        int tamMsg1 = std::stoi(pkgs[0].substr(16, 5));
        int tamDest = std::stoi(pkgs[0].substr(21 + tamMsg1, 5));
        std::string destino = pkgs[0].substr(21 + tamMsg1 + 5, tamDest);

        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(16, 5));
            mensaje += pkgs[i].substr(21, t);
        }

        sockaddr_in dest = mapaAddr[destino];
        std::vector<std::string> piezas = partir(mensaje, 500 - 31 - nickname.length());
        int totalPkg = piezas.size();
        int seq = 1;

        for (auto &trozo : piezas) {
            char buffer[500];
            std::memset(buffer, '#', 500);
            // Cabecera 5-5-5
            std::sprintf(buffer, "%05d", seq);            // Secuencia
            std::sprintf(buffer + 5, "%05d", totalPkg);   // Total
            int totalSize = 1 + 5 + trozo.length() + 5 + nickname.length();
            std::sprintf(buffer + 10, "%05d", totalSize); // Tamaño
            buffer[15] = 'm';                             // Tipo
            std::sprintf(buffer + 16, "%05d", (int)trozo.length());
            std::memcpy(buffer + 21, trozo.c_str(), trozo.length());
            int offset = 21 + trozo.length();
            std::sprintf(buffer + offset, "%05d", (int)nickname.length());
            offset += 5;
            std::memcpy(buffer + offset, nickname.c_str(), nickname.length());

            sendto(sock, buffer, 500, 0, (sockaddr *)&dest, sizeof(dest));
            ++seq;
        }
    }
    else if (tipo == 'B') {
        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(16, 5));
            mensaje += pkgs[i].substr(21, t);
        }

        std::vector<std::string> piezas = partir(mensaje, 500 - 21 - nickname.length());
        int totalPkg = piezas.size();
        int seq = 1;

        for (auto &trozo : piezas) {
            char buffer[500];
            std::memset(buffer, '#', 500);
            // Cabecera 5-5-5
            std::sprintf(buffer, "%05d", seq);            // Secuencia
            std::sprintf(buffer + 5, "%05d", totalPkg);   // Total
            int totalSize = 1 + 5 + trozo.length() + 5 + nickname.length();
            std::sprintf(buffer + 10, "%05d", totalSize); // Tamaño
            buffer[15] = 'b';                             // Tipo
            std::sprintf(buffer + 16, "%05d", (int)trozo.length());
            std::memcpy(buffer + 21, trozo.c_str(), trozo.length());
            int offset = 21 + trozo.length();
            std::sprintf(buffer + offset, "%05d", (int)nickname.length());
            offset += 5;
            std::memcpy(buffer + offset, nickname.c_str(), nickname.length());

            for (const auto &par : mapaAddr) {
                if (par.first == nickname) continue;
                const sockaddr_in &dest = par.second;
                sendto(sock, buffer, 500, 0, (const sockaddr *)&dest, sizeof(dest));
            }
            ++seq;
        }
    }
    else if (tipo == 'F') {
        int tamDest = std::stoi(pkgs[0].substr(16, 5));
        std::string destino = pkgs[0].substr(21, tamDest);
        int tamFileName = std::stoi(pkgs[0].substr(21 + tamDest, 100));
        std::string fileName = pkgs[0].substr(21 + tamDest + 100, tamFileName);
        long tamFile = std::stol(pkgs[0].substr(21 + tamDest + 100 + tamFileName, 18));
        std::string hash = pkgs[0].substr(21 + tamDest + 100 + tamFileName + 18 + tamFile, 5);

        std::string archivo;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            long lenTrozo = std::stol(pkgs[i].substr(21 + tamDest + 100 + tamFileName, 18));
            size_t posDatos = 21 + tamDest + 100 + tamFileName + 18;
            archivo += pkgs[i].substr(posDatos, lenTrozo);
        }

        std::vector<std::string> piezas = partir(archivo, 500 - 138 - nickname.length() - fileName.length());
        int totalPkg = piezas.size();
        int seq = 1;

        for (auto &trozo : piezas) {
            char buffer[500];
            std::memset(buffer, '#', 500);
            // Cabecera 5-5-5
            std::sprintf(buffer, "%05d", seq);            // Secuencia
            std::sprintf(buffer + 5, "%05d", totalPkg);   // Total
            int totalSize = 1 + 5 + nickname.length() + 100 + fileName.length() + 18 + trozo.length() + 5;
            std::sprintf(buffer + 10, "%05d", totalSize); // Tamaño
            buffer[15] = 'f';                             // Tipo
            std::sprintf(buffer + 16, "%05d", (int)nickname.length());
            std::memcpy(buffer + 21, nickname.c_str(), nickname.length());
            int offset = 21 + nickname.length();
            std::sprintf(buffer + offset, "%0100d", (int)fileName.length());
            offset += 100;
            std::memcpy(buffer + offset, fileName.c_str(), fileName.length());
            offset += fileName.length();
            std::sprintf(buffer + offset, "%018d", (int)trozo.length());
            offset += 18;
            std::memcpy(buffer + offset, trozo.c_str(), trozo.length());
            offset += trozo.length();
            std::memcpy(buffer + offset, hash.c_str(), 5);

            sockaddr_in dest = mapaAddr[destino];
            sendto(sock, buffer, 500, 0, (sockaddr *)&dest, sizeof(dest));
            ++seq;
        }
    }
    else if (tipo == 'Q') {
        printf("\nEl cliente %s ha salido del chat\n", nickname.c_str());
        mapaAddr.erase(nickname);
    }
    else if (tipo == 'J') {
        if (!partida.activa && jugadorEnEspera.empty()) {
            jugadorEnEspera = nickname;
            enviarM(sock, mapaAddr[nickname], "wait for player");
        }
        else if (!partida.activa && !jugadorEnEspera.empty()) {
            partida.activa = true;
            partida.jugadorO = jugadorEnEspera;
            partida.jugadorX = nickname;
            jugadorEnEspera = "";

            enviarM(sock, mapaAddr[partida.jugadorO], "inicio");
            enviarM(sock, mapaAddr[partida.jugadorX], "inicio");
            enviarX_aTodos(sock);
            enviarT(sock, partida.jugadorO, 'O');
        }
        else {
            enviarM(sock, mapaAddr[nickname], "do you want to see?");
        }
    }
    else if (tipo == 'V') {
        if (partida.activa) {
            partida.espectadores.push_back(nickname);
            char pkt[500];
            std::memset(pkt, '#', 500);
            // Cabecera 5-5-5
            std::memcpy(pkt, "00001", 5);      // Secuencia
            std::memcpy(pkt + 5, "00001", 5);  // Total
            std::memcpy(pkt + 10, "00010", 5); // Tamaño
            pkt[15] = 'X';                     // Tipo
            std::memcpy(pkt + 16, partida.tablero, 9);
            sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[nickname], sizeof(sockaddr_in));
        }
    }
    else if (tipo == 'P') {
        char pos = pkgs[0][16];  // Offset 16 para datos
        char simb = pkgs[0][17];
        int idx = pos - '1';

        if (idx < 0 || idx > 8 || partida.tablero[idx] != '_') {
            char errPkt[500];
            std::memset(errPkt, '#', 500);
            // Cabecera 5-5-5
            std::memcpy(errPkt, "00001", 5);      // Secuencia
            std::memcpy(errPkt + 5, "00001", 5);  // Total
            std::memcpy(errPkt + 10, "00008", 5); // Tamaño
            errPkt[15] = 'E';                     // Tipo
            errPkt[16] = '6';
            std::memcpy(errPkt + 17, "00016", 5);
            std::memcpy(errPkt + 22, "Posicion ocupada", 16);
            sendto(sock, errPkt, 500, 0, (const sockaddr *)&mapaAddr[nickname], sizeof(sockaddr_in));
            return;
        }

        partida.tablero[idx] = simb;

        if (ganador(simb)) {
            enviarX_aTodos(sock);
            sleep(1);

            auto enviaResultado = [&](const std::string &nick, char res) {
                char pkt[500];
                std::memset(pkt, '#', 500);
                // Cabecera 5-5-5
                std::memcpy(pkt, "00001", 5);      // Secuencia
                std::memcpy(pkt + 5, "00001", 5);  // Total
                std::memcpy(pkt + 10, "00002", 5); // Tamaño
                pkt[15] = 'O';                     // Tipo
                pkt[16] = res;
                sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[nick], sizeof(sockaddr_in));
            };

            enviaResultado(partida.jugadorO, (simb == 'O') ? 'W' : 'L');
            enviaResultado(partida.jugadorX, (simb == 'X') ? 'W' : 'L');
            for (auto &esp : partida.espectadores)
                enviaResultado(esp, 'E');

            partida = Partida();
        }
        else if (tableroLleno()) {
            enviarX_aTodos(sock);
            char pkt[500];
            std::memset(pkt, '#', 500);
            // Cabecera 5-5-5
            std::memcpy(pkt, "00001", 5);      // Secuencia
            std::memcpy(pkt + 5, "00001", 5);  // Total
            std::memcpy(pkt + 10, "00002", 5); // Tamaño
            pkt[15] = 'O';                     // Tipo
            pkt[16] = 'E';
            for (auto nick : {partida.jugadorO, partida.jugadorX})
                sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[nick], sizeof(sockaddr_in));
            for (auto &esp : partida.espectadores)
                sendto(sock, pkt, 500, 0, (sockaddr *)&mapaAddr[esp], sizeof(sockaddr_in));

            partida = Partida();
        }
        else {
            enviarX_aTodos(sock);
            partida.turno = (partida.turno == 'O') ? 'X' : 'O';
            const std::string &prox = (partida.turno == 'O') ? partida.jugadorO : partida.jugadorX;
            enviarT(sock, prox, partida.turno);
        }
    }
}

int main(void) {
    int sock;
    struct sockaddr_in server_addr, client_addr;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(server_addr.sin_zero), 8);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("Bind");
        exit(1);
    }

    sockaddr_in cli;
    socklen_t cliLen = sizeof(cli);

    for (;;) {
        char buffer[500];
        int n = recvfrom(sock, buffer, 500, 0, (sockaddr *)&cli, &cliLen);
        if (n != 500) {
            std::cout << "ERROR size mensaje\n";
            continue;
        }

        char clave[32];
        sprintf(clave, "%s:%d", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        std::string pkg(buffer, 500);

        if (pkg[15] == 'N') {  // Registro (tipo en offset 15)
            int tamNick = std::stoi(pkg.substr(10, 5)) - 1;
            std::string nick = pkg.substr(20, tamNick);  // Datos en offset 20
            mapaAddr[nick] = cli;
            std::cout << "El cliente " << nick << " se ha unido al chat\n";
            continue;
        }

        std::string nick;
        for (auto &kv : mapaAddr)
            if (memcmp(&kv.second, &cli, sizeof(cli)) == 0) {
                nick = kv.first;
                break;
            }
        if (nick.empty()) continue;

        int seq = std::stoi(pkg.substr(0, 5));    // Secuencia (5 bytes)
        int tot = std::stoi(pkg.substr(5, 5));    // Total (5 bytes)
        int idx = (seq == 0) ? tot - 1 : seq - 1;

        auto &vc = clientePkgs[clave];
        if (vc.empty()) vc.resize(tot);
        if (idx >= tot || !vc[idx].empty()) continue;
        vc[idx] = std::move(pkg);

        bool completo = true;
        for (auto &p : vc)
            if (p.empty()) { completo = false; break; }

        if (completo) {
            char tipo = vc[0][15];  // Tipo en offset 15
            std::vector<std::string> partes = std::move(vc);
            clientePkgs.erase(clave);

            std::thread(procesarMensajes, std::move(partes), nick, sock, tipo).detach();
        }
    }
    close(sock);
    return 0;
}