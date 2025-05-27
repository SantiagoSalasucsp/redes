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

        int seq = std::stoi(pkg.substr(0, 5));
        int tot = std::stoi(pkg.substr(5, 5));
        int tamTot = std::stoi(pkg.substr(10, 5));
        char tipo = pkg[15];

        int idx = (seq == 0) ? tot - 1 : seq - 1;
        if (idx < 0 || idx >= tot) continue;

        if (vc.empty()) vc.resize(tot);
        if (idx >= (int)vc.size()) continue;
        if (!vc[idx].empty()) continue;
        vc[idx] = std::move(pkg);

        bool completo = true;
        for (auto &p : vc) {
            if (p.empty()) {
                completo = false;
                break;
            }
        }

        if (!completo) continue;

        std::thread(procesarMensajes, std::move(vc), nickname, sock, tipo).detach();
        vc.clear();
    }
}

void procesarMensajes(std::vector<std::string> pkgs, std::string nickname, int sock, char tipo) {
    char buffer[500];

    if (tipo == 'l') {
        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(16, 5)) - 1;
            mensaje += pkgs[i].substr(21, t);
        }
        printf("\nUsuarios conectados: %s\n", mensaje.c_str());
        envLista = true;
        return;
    }
    else if (tipo == 'm') {
        int tamMsg1 = std::stoi(pkgs[0].substr(16, 5));
        int tamOrig = std::stoi(pkgs[0].substr(21 + tamMsg1, 5));
        std::string usuarioOrigen = pkgs[0].substr(21 + tamMsg1 + 5, tamOrig);

        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(16, 5));
            mensaje += pkgs[i].substr(21, t);
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
            return;
        }
    }
    else if (tipo == 'b') {
        int tamMsg1 = std::stoi(pkgs[0].substr(16, 5));
        int tamOrig = std::stoi(pkgs[0].substr(21 + tamMsg1, 5));
        std::string usuarioOrigen = pkgs[0].substr(21 + tamMsg1 + 5, tamOrig);

        std::string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            int t = std::stoi(pkgs[i].substr(16, 5));
            mensaje += pkgs[i].substr(21, t);
        }

        printf("\n\n[broadcast] %s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());
    }
    else if (tipo == 'f') {
        int tamEmisor = std::stoi(pkgs[0].substr(16, 5));
        std::string emisor = pkgs[0].substr(21, tamEmisor);
        int tamFileName = std::stoi(pkgs[0].substr(21 + tamEmisor, 100));
        std::string fileName = pkgs[0].substr(21 + tamEmisor + 100, tamFileName);
        long tamFile = std::stol(pkgs[0].substr(21 + tamEmisor + 100 + tamFileName, 18));
        std::string hash = pkgs[0].substr(21 + tamEmisor + 100 + tamFileName + 18 + tamFile, 5);

        std::string archivo;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            long lenTrozo = std::stol(pkgs[i].substr(21 + tamEmisor + 100 + tamFileName, 18));
            size_t posDatos = 21 + tamEmisor + 100 + tamFileName + 18;
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
            char c = pkgs[0][16 + j];
            std::putchar(c);
            std::putchar((j % 3 == 2) ? '\n' : '|');
        }
    }
    else if (tipo == 't' || tipo == 'T') {
        simbolo = pkgs[0][16];
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
        char codigo = pkgs[0][16];
        int tamMsg1 = std::stoi(pkgs[0].substr(17, 5));
        std::string mensaje = pkgs[0].substr(22, tamMsg1);
        std::cout << "\nERROR " << codigo << ": " << mensaje << "\n";
        if (codigo == '6') {
            tuTurno = true;
        }
    }
    else if (tipo == 'o' || tipo == 'O') {
        char res = pkgs[0][16];
        if (!esEspectador) {
            if (res == 'W')
                puts("\n*** ¡Ganaste! ***");
            else if (res == 'L')
                puts("\n*** Perdiste ***");
            else
                puts("\n*** Empate ***");
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
    std::memcpy(buffer, "00001", 5);
    std::memcpy(buffer + 5, "00001", 5);
    std::memcpy(buffer + 10, "00008", 5);
    int off = 15;
    buffer[off++] = 'P';
    buffer[off++] = pos + '0';
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
    std::memcpy(buffer, "00001", 5);
    std::memcpy(buffer + 5, "00001", 5);
    int tamNick = nickname.length() + 1;
    std::sprintf(buffer + 10, "%05d", 1 + tamNick);
    buffer[15] = 'N';
    std::memcpy(buffer + 16, nickname.c_str(), tamNick);
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
                std::memcpy(buffer, "00001", 5);
                std::memcpy(buffer + 5, "00001", 5);
                std::memcpy(buffer + 10, "00001", 5);
                buffer[15] = 'V';
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
            continue;
        }

        switch (opcion) {
            case 'a': {
                std::memset(buffer, '#', 500);
                std::memcpy(buffer, "00001", 5);
                std::memcpy(buffer + 5, "00001", 5);
                std::memcpy(buffer + 10, "00001", 5);
                buffer[15] = 'L';
                if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                    perror("sendto");
                while (!envLista);
                envLista = false;
                break;
            }
            case 'b': {
                std::string destino;
                std::string mensaje;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "\nNombre Destinatario: ";
                std::getline(std::cin, destino);
                std::cout << "\nMensaje: ";
                std::getline(std::cin, mensaje);

                int tamMsg = mensaje.length();
                int tamDest = destino.length();
                int sizeFijo = 31 + tamDest;
                int tamMax = 500 - sizeFijo;

                std::vector<std::string> piezas = partir(mensaje, tamMax);
                int totalPkg = piezas.size();
                int seq = 1;

                for (auto &trozo : piezas) {
                    std::memset(buffer, '#', 500);
                    std::sprintf(buffer, "%05d", seq);
                    std::sprintf(buffer + 5, "%05d", totalPkg);
                    int totalSize = 1 + 5 + trozo.length() + 5 + destino.length();
                    std::sprintf(buffer + 10, "%05d", totalSize);
                    buffer[15] = 'M';
                    std::sprintf(buffer + 16, "%05d", (int)trozo.length());
                    std::memcpy(buffer + 21, trozo.c_str(), trozo.length());
                    int offset = 21 + trozo.length();
                    std::sprintf(buffer + offset, "%05d", (int)destino.length());
                    offset += 5;
                    std::memcpy(buffer + offset, destino.c_str(), destino.length());

                    sendto(sock, buffer, 500, 0, (sockaddr *)&server_addr, sizeof(server_addr));
                    ++seq;
                }
                break;
            }
            case 'c': {
                salir = true;
                std::memset(buffer, '#', 500);
                std::memcpy(buffer, "00001", 5);
                std::memcpy(buffer + 5, "00001", 5);
                std::memcpy(buffer + 10, "00001", 5);
                buffer[15] = 'Q';
                if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                    perror("sendto");
                break;
            }
            case 'd': {
                std::string mensaje;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "\nMensaje: ";
                std::getline(std::cin, mensaje);

                int sizeFijo = 21;
                int tamMax = 500 - sizeFijo;

                std::vector<std::string> piezas = partir(mensaje, tamMax);
                int totalPkg = piezas.size();
                int seq = 1;

                for (auto &trozo : piezas) {
                    std::memset(buffer, '#', 500);
                    std::sprintf(buffer, "%05d", seq);
                    std::sprintf(buffer + 5, "%05d", totalPkg);
                    int totalSize = 1 + 5 + trozo.length();
                    std::sprintf(buffer + 10, "%05d", totalSize);
                    buffer[15] = 'B';
                    std::sprintf(buffer + 16, "%05d", (int)trozo.length());
                    std::memcpy(buffer + 21, trozo.c_str(), trozo.length());

                    sendto(sock, buffer, 500, 0, (sockaddr *)&server_addr, sizeof(server_addr));
                    ++seq;
                }
                break;
            }
            case 'e': {
                std::string destino;
                std::string rutaArchivo;
                std::string nombreArchivo;

                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "\nNombre Destinatario: ";
                std::getline(std::cin, destino);
                std::cout << "Ruta del archivo a enviar: ";
                std::getline(std::cin, rutaArchivo);

                size_t posBarra = rutaArchivo.find_last_of("/\\");
                nombreArchivo = (posBarra != std::string::npos) ? rutaArchivo.substr(posBarra + 1) : rutaArchivo;

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

                std::vector<std::string> piezas = partir(archivo, tamMax);
                int totalPkg = piezas.size();
                int seq = 1;

                for (auto &trozo : piezas) {
                    std::memset(buffer, '#', 500);
                    std::sprintf(buffer, "%05d", seq);
                    std::sprintf(buffer + 5, "%05d", totalPkg);
                    int totalSize = 1 + 5 + destino.length() + 100 + nombreArchivo.length() + 18 + trozo.length() + 5;
                    std::sprintf(buffer + 10, "%05d", totalSize);
                    buffer[15] = 'F';
                    std::sprintf(buffer + 16, "%05d", (int)destino.length());
                    std::memcpy(buffer + 21, destino.c_str(), destino.length());
                    int offset = 21 + destino.length();
                    std::sprintf(buffer + offset, "%0100d", (int)nombreArchivo.length());
                    offset += 100;
                    std::memcpy(buffer + offset, nombreArchivo.c_str(), nombreArchivo.length());
                    offset += nombreArchivo.length();
                    std::sprintf(buffer + offset, "%018ld", trozo.length());
                    offset += 18;
                    std::memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();
                    std::memcpy(buffer + offset, hash.c_str(), 5);

                    sendto(sock, buffer, 500, 0, (sockaddr *)&server_addr, sizeof(server_addr));
                    printf("Enviando mensaje %05d%05d\n", seq, totalPkg);
                    ++seq;
                }
                break;
            }
            case 'f': {
                if (!enPartida) {
                    std::memset(buffer, '#', 500);
                    std::memcpy(buffer, "00001", 5);
                    std::memcpy(buffer + 5, "00001", 5);
                    std::memcpy(buffer + 10, "00001", 5);
                    buffer[15] = 'J';
                    if (sendto(sock, buffer, 500, 0, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1)
                        perror("sendto");
                    enPartida = true;
                }
                else {
                    std::cout << "Ya estas en partida.\n";
                }
                break;
            }
            default: {
                std::cout << "Comando no valido.\n";
            }
        }
    }
    return 0;
}