
/* Client UDP code in C++ */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <limits>
#include <fstream>
#include <vector>
#include <mutex>
#include <cstring>

std::string nickname;
std::atomic<bool> envLista(false);
std::atomic<bool> enPartida(false);
std::atomic<bool> esEspectador(false);
std::atomic<char> simbolo{'\0'};
std::atomic<bool> tuTurno(false);
std::atomic<bool> preguntaVer(false);
std::mutex coutMux;
int socketGlobal;

void procesarMensajes(std::vector<std::string> pkgs, std::string nickname, int sock, char tipo);

std::vector<std::string> partir(std::string msg, int tamMax) {
    std::vector<std::string> partes;
    int len = msg.size();

    for (int i = 0; i < len; i += tamMax) {
        partes.push_back(msg.substr(i, tamMax));
    }

    return partes;
}

int calcularHash(const char *datos, long tamano) {
    int hash = 0;
    for (long i = 0; i < tamano; i++) {
        hash = (hash + (unsigned char)datos[i]) % 100000;
    }
    return hash;
}

void recibirMensajes(int sock, sockaddr_in server_addr) {
    char buffer[500];
    static std::vector<std::string> vc;
    vc.clear();

    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);

    while (true) {
        int n = recvfrom(sock, buffer, 500, 0, (struct sockaddr *)&from_addr, &addr_len);
        if (n != 500) {
            std::cout << "ERROR size mensaje\n";
            continue;
        }

        std::string pkg(buffer, 500);
        int seq = std::stoi(pkg.substr(0, 2));
        int tot = std::stoi(pkg.substr(2, 2));
        int idx = (seq == 0) ? tot - 1 : seq - 1;

        if (idx < 0 || idx >= tot) continue;
        if (vc.empty()) vc.resize(tot);
        if (idx >= (int)vc.size()) continue;
        if (!vc[idx].empty()) continue;
        vc[idx] = std::move(pkg);

        bool completo = true;
        for (auto &p : vc)
            if (p.empty()) {
                completo = false;
                break;
            }

        if (!completo) continue;

        char tipo = vc[0][9];
        std::vector<std::string> partes = std::move(vc);
        vc.clear();

        std::thread(procesarMensajes, std::move(partes), nickname, sock, tipo).detach();
    }
}

void procesarMensajes(std::vector<std::string> pkgs, std::string nickname, int sock, char tipo) {
    if (tipo == 'l') {
        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(4, 5)) - 1;
            mensaje += pkgs[i].substr(10, t);
        }
        printf("\nUsuarios conectados: %s\n", mensaje.c_str());
        envLista = true;
    }
    else if (tipo == 'm') {
        int tamMsg1 = std::stoi(pkgs[0].substr(10, 5));
        int tamOrig = std::stoi(pkgs[0].substr(15 + tamMsg1, 5));
        std::string usuarioOrigen = pkgs[0].substr(15 + tamMsg1 + 5, tamOrig);

        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(10, 5));
            mensaje += pkgs[i].substr(15, t);
        }

        printf("\n\n%s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());

        if ((usuarioOrigen == "servidor" || usuarioOrigen == "Servidor") &&
            (mensaje == "do you want to see?" || mensaje == "Do you want to see?" ||
             mensaje == "Do you want to see" || mensaje == "Desea ver?" ||
             mensaje == "do you want to see")) {
            {
                std::lock_guard<std::mutex> l(coutMux);
                std::cout << "y. Sí   n. No\n";
                std::cout.flush();
            }
            preguntaVer = true;
        }
    }
    else if (tipo == 'b') {
        int tamMsg1 = std::stoi(pkgs[0].substr(10, 5));
        int tamOrig = std::stoi(pkgs[0].substr(15 + tamMsg1, 5));
        std::string usuarioOrigen = pkgs[0].substr(15 + tamMsg1 + 5, tamOrig);

        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(10, 5));
            mensaje += pkgs[i].substr(15, t);
        }

        printf("\n\n[broadcast] %s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());
    }
    else if (tipo == 'f') {
        int tamEmisor = std::stoi(pkgs[0].substr(10, 5));
        std::string emisor = pkgs[0].substr(15, tamEmisor);
        int tamFileName = std::stoi(pkgs[0].substr(15 + tamEmisor, 100));
        std::string fileName = pkgs[0].substr(15 + tamEmisor + 100, tamFileName);
        long tamFile = std::stol(pkgs[0].substr(15 + tamEmisor + 100 + tamFileName, 18));
        std::string hash = pkgs[0].substr(15 + tamEmisor + 100 + tamFileName + 18 + tamFile, 5);

        std::string archivo;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            long lenTrozo = std::stol(pkgs[i].substr(15 + tamEmisor + 100 + tamFileName, 18));
            size_t posDatos = 15 + tamEmisor + 100 + tamFileName + 18;
            archivo += pkgs[i].substr(posDatos, lenTrozo);
        }

        int tamanoArchivo = archivo.length();
        int hashLocal = calcularHash(archivo.c_str(), tamanoArchivo);

        if (std::stoi(hash) == hashLocal)
            printf("\n[archivo] %s: %s (Hash OK)\n", emisor.c_str(), fileName.c_str());
        else
            printf("\n[archivo] %s: %s (Hash INCORRECTO: calculado %d, recibido %s)\n",
                   emisor.c_str(), fileName.c_str(), hashLocal, hash.c_str());

        if (std::stoi(hash) == hashLocal) {
            std::ofstream archivoSalida(fileName, std::ios::binary);
            if (!archivoSalida.is_open()) {
                std::cerr << "No se pudo crear el archivo " << fileName << " en disco.\n";
            }
            else {
                archivoSalida.write(archivo.c_str(), tamanoArchivo);
                archivoSalida.close();
                std::cout << "Archivo " << fileName << " guardado en la carpeta actual.\n";
            }
        }
    }
    else if (tipo == 'x' || tipo == 'X') {
        puts("\nTABLERO");
        for (int j = 0; j < 9; ++j) {
            char c = pkgs[0][10 + j];
            std::putchar(c);
            std::putchar((j % 3 == 2) ? '\n' : '|');
        }
    }
    else if (tipo == 't' || tipo == 'T') {
        simbolo = pkgs[0][10];
        tuTurno = true;
        {
            std::lock_guard<std::mutex> l(coutMux);
            std::cout << "\n───────────────────────────────\n"
                         "Tu turno ("
                      << simbolo << ")\n Ingresa una posicion [1-9]: \n";
            std::cout.flush();
        }
    }
    else if (tipo == 'e' || tipo == 'E') {
        char codigo = pkgs[0][10];
        int tamMsg1 = std::stoi(pkgs[0].substr(11, 5));
        std::string mensaje = pkgs[0].substr(11 + 5, tamMsg1);
        std::cout << "\nERROR " << codigo << ": " << mensaje << "\n";
        if (codigo == '6') tuTurno = true;
    }
    else if (tipo == 'o' || tipo == 'O') {
        char res = pkgs[0][10];
        if (!esEspectador) {
            if (res == 'W') puts("\n*** ¡Ganaste! ***");
            else if (res == 'L') puts("\n*** Perdiste ***");
            else puts("\n*** Empate ***");
        }
        else {
            puts("\n*** La partida termino ***");
        }
        enPartida = false;
        esEspectador = false;
    }
}

void enviarP(int sock, const sockaddr_in &server_addr, int pos, char simbolo) {
    char buffer[500];
    std::memset(buffer, '#', 500);
    int tamTot = 3;

    std::memcpy(buffer, "0001", 4);
    std::sprintf(buffer + 4, "%05d", tamTot);
    int off = 9;
    char posChar = pos + '0';
    buffer[off++] = 'P';
    buffer[off++] = posChar;
    buffer[off++] = simbolo;

    if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        perror("sendto");
}

int main(void) {
    int sock;
    struct sockaddr_in server_addr;
    struct hostent *host;
    char buffer[500];

    host = (struct hostent *)gethostbyname((char *)"127.0.0.1");

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(server_addr.sin_zero), 8);

    socketGlobal = sock;

    std::cout << "Ingresa tu nickname: ";
    std::getline(std::cin, nickname);
    std::memset(buffer, '#', 500);
    sprintf(buffer, "0001%05dN%s\0", (int)nickname.length() + 1, nickname.c_str());
    sendto(sock, buffer, 500, 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
    std::thread(recibirMensajes, sock, server_addr).detach();

    bool salir = false;

    while (!salir) {
        {
            std::lock_guard<std::mutex> l(coutMux);
            if (!enPartida) {
                std::cout << "\n=== MENÚ PRINCIPAL ===\n"
                             "a) Ver lista de usuarios\n"
                             "b) Mandar mensaje privado\n"
                             "c) Salir\n"
                             "d) Mandar broadcast\n"
                             "e) Mandar archivo\n"
                             "f) Unirse a Tic-Tac-Toe\n";
            }
            if (tuTurno)
                std::cout << "(Es tu turno, teclea 1-9 para marcar)\n";
            if (preguntaVer) {
                std::cout << "do you want to see?\n";
            }
            std::cout << "> ";
            std::cout.flush();
        }

        char opcion;
        std::cin >> opcion;

        if (tuTurno && opcion >= '1' && opcion <= '9') {
            enviarP(sock, server_addr, opcion - '0', simbolo);
            tuTurno = false;
            continue;
        }

        if (preguntaVer) {
            if (opcion == 'y' || opcion == 'Y') {
                esEspectador = true;
                std::memset(buffer, '#', 500);
                std::memcpy(buffer, "0001", 4);
                std::sprintf(buffer + 4, "%05d", 1);
                int off = 9;
                buffer[off++] = 'V';

                if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                    perror("sendto");
            }
            else if (opcion == 'n' || opcion == 'N') {
                enPartida = false;
            }
            else {
                std::cout << "Comando no valido pp.\n";
                continue;
            }
            preguntaVer = false;
        }
        else if (opcion == 'a') {
            std::memset(buffer, '#', 500);
            strcpy(buffer, "000100001L");
            if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                perror("sendto");
            while (!envLista);
            envLista = false;
        }
        else if (opcion == 'b') {
            std::string destino;
            std::string mensaje;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\nNombre Destinatario: ";
            std::getline(std::cin, destino);
            std::cout << "\nMensaje: ";
            std::getline(std::cin, mensaje);

            int tamMsg = mensaje.length();
            int tamDest = destino.length();
            int sizeFijo = 20 + tamDest;
            int tamMax = 500 - sizeFijo;

            std::vector<std::string> piezas = (mensaje.length() > tamMax)
                ? partir(mensaje, tamMax)
                : std::vector<std::string>{mensaje};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                std::memset(buffer, '#', 500);
                std::sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + trozo.length() + 5 + destino.length();
                int offset = 4;
                std::sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'M';
                std::sprintf(buffer + offset, "%05d", (int)trozo.length());
                offset += 5;
                std::memcpy(buffer + offset, trozo.c_str(), trozo.length());
                offset += trozo.length();
                std::sprintf(buffer + offset, "%05d", (int)destino.length());
                offset += 5;
                std::memcpy(buffer + offset, destino.c_str(), destino.length());
                sendto(sock, buffer, 500, 0, (sockaddr *)&server_addr, sizeof(server_addr));
                ++seq;
            }
        }
        else if (opcion == 'c') {
            salir = true;
            std::memset(buffer, '#', 500);
            strcpy(buffer, "000100001Q");
            if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                perror("sendto");
        }
        else if (opcion == 'd') {
            std::string mensaje;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\nMensaje: ";
            std::getline(std::cin, mensaje);

            int sizeFijo = 15;
            int tamMax = 500 - sizeFijo;

            std::vector<std::string> piezas = (mensaje.length() > tamMax)
                ? partir(mensaje, tamMax)
                : std::vector<std::string>{mensaje};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                std::memset(buffer, '#', 500);
                std::sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + trozo.length();
                int offset = 4;
                std::sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'B';
                std::sprintf(buffer + offset, "%05d", (int)trozo.length());
                offset += 5;
                std::memcpy(buffer + offset, trozo.c_str(), trozo.length());
                sendto(sock, buffer, 500, 0, (sockaddr *)&server_addr, sizeof(server_addr));
                ++seq;
            }
        }
        else if (opcion == 'e') {
            std::string destino;
            std::string rutaArchivo;
            std::string nombreArchivo;

            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\nNombre Destinatario: ";
            std::getline(std::cin, destino);
            std::cout << "Ruta del archivo a enviar: ";
            std::getline(std::cin, rutaArchivo);

            size_t posBarra = rutaArchivo.find_last_of("/\\");
            if (posBarra != std::string::npos) {
                nombreArchivo = rutaArchivo.substr(posBarra + 1);
            }
            else {
                nombreArchivo = rutaArchivo;
            }

            std::ifstream archivoEntrada(rutaArchivo, std::ios::in | std::ios::binary);
            if (!archivoEntrada.is_open()) {
                std::cerr << "No se pudo abrir el archivo " << rutaArchivo << "\n";
                continue;
            }
            std::vector<char> datosArchivo((std::istreambuf_iterator<char>(archivoEntrada)),
                std::istreambuf_iterator<char>());
            long tamanoArchivo = datosArchivo.size();
            archivoEntrada.close();

            int sizeFijo = 138 + destino.length() + nombreArchivo.length();
            int tamMax = 500 - sizeFijo;

            int valorHash = calcularHash(datosArchivo.data(), tamanoArchivo);
            std::string hash = std::to_string(valorHash);

            std::string archivo(datosArchivo.begin(), datosArchivo.end());
            std::vector<std::string> piezas = (archivo.length() > tamMax)
                ? partir(archivo, tamMax)
                : std::vector<std::string>{archivo};

            int totalPkg = piezas.size();
            std::cout << "Total de paquetes a enviar: " << totalPkg << "\n";

            int seq = 1;
            for (auto &trozo : piezas) {
                std::memset(buffer, '#', 500);
                std::sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + destino.length() + 100 + nombreArchivo.length() + 18 + trozo.length() + 5;
                int offset = 4;
                std::sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'F';
                std::sprintf(buffer + offset, "%05d", (int)destino.length());
                offset += 5;
                std::memcpy(buffer + offset, destino.c_str(), destino.length());
                offset += destino.length();
                std::sprintf(buffer + offset, "%0100d", (int)nombreArchivo.length());
                offset += 100;
                std::memcpy(buffer + offset, nombreArchivo.c_str(), nombreArchivo.length());
                offset += nombreArchivo.length();
                std::sprintf(buffer + offset, "%018d", (int)trozo.length());
                offset += 18;
                std::memcpy(buffer + offset, trozo.c_str(), trozo.length());
                offset += trozo.length();
                std::memcpy(buffer + offset, hash.c_str(), 5);
                sendto(sock, buffer, 500, 0, (sockaddr *)&server_addr, sizeof(server_addr));
                printf("Enviando mensaje %02d%02d\n", seq, totalPkg);
                ++seq;
            }
        }
        else if (opcion == 'f') {
            if (!enPartida) {
                std::memset(buffer, '#', 500);
                strcpy(buffer, "000100001J");
                if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                    perror("sendto");
                enPartida = true;
            }
            else {
                std::cout << "Ya estas en partida.\n";
            }
        }
        else {
            std::cout << "Comando no valido.\n";
        }
    }
    return 0;
}

