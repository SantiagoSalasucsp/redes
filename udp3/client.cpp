/* Client code in C - TCP version with UDP-style protocol */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Para memset y memcpy
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

std::string nickname;
std::atomic<bool> envLista(false);
std::atomic<bool> enPartida(false);
std::atomic<bool> esEspectador(false);
std::atomic<char> simbolo{'\0'};
std::atomic<bool> tuTurno(false);
std::atomic<bool> preguntaVer(false);
std::mutex coutMux;
int socketGlobal;

// calcular hash (suma de bytes mod 100000)
int calcularHash(const char *datos, long tamano) {
    int hash = 0;
    for (long i = 0; i < tamano; i++) {
        hash = (hash + (unsigned char)datos[i]) % 100000;
    }
    return hash;
}

std::vector<std::string> partir(std::string msg, int tamMax) {
    std::vector<std::string> partes;
    int len = msg.size();

    for (int i = 0; i < len; i += tamMax) {
        partes.push_back(msg.substr(i, tamMax));
    }

    return partes;
}

void procesarMensaje(std::string pkg) {
    char tipo = pkg[9]; // byte 9 es el tipo

    if (tipo == 'l') {
        int tamMsg = std::stoi(pkg.substr(4, 5)) - 1;
        std::string mensaje = pkg.substr(10, tamMsg);
        printf("\nUsuarios conectados: %s\n", mensaje.c_str());
        envLista = true;
    }
    else if (tipo == 'm') {
        int tamMsg = std::stoi(pkg.substr(10, 5));
        std::string mensaje = pkg.substr(15, tamMsg);
        int tamOrig = std::stoi(pkg.substr(15 + tamMsg, 5));
        std::string usuarioOrigen = pkg.substr(15 + tamMsg + 5, tamOrig);

        printf("\n\n%s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());

        if ((usuarioOrigen == "servidor" || usuarioOrigen == "Servidor") &&
            (mensaje == "do you want to see?" || mensaje == "Do you want to see?" || 
             mensaje == "Do you want to see" || mensaje == "Desea ver?" || 
             mensaje == "do you want to see")) {
            {
                std::lock_guard<std::mutex> l(coutMux);
                std::cout << "y. Sí  n. No\n";
                std::cout.flush();
            }
            preguntaVer = true;
        }
    }
    else if (tipo == 'b') {
        int tamMsg = std::stoi(pkg.substr(10, 5));
        std::string mensaje = pkg.substr(15, tamMsg);
        int tamOrig = std::stoi(pkg.substr(15 + tamMsg, 5));
        std::string usuarioOrigen = pkg.substr(15 + tamMsg + 5, tamOrig);

        printf("\n\n[broadcast] %s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());
    }
    else if (tipo == 'f') {
        int tamEmisor = std::stoi(pkg.substr(10, 5));
        std::string emisor = pkg.substr(15, tamEmisor);
        int tamFileName = std::stoi(pkg.substr(15 + tamEmisor, 100));
        std::string fileName = pkg.substr(15 + tamEmisor + 100, tamFileName);
        long tamFile = std::stol(pkg.substr(15 + tamEmisor + 100 + tamFileName, 18));
        std::string hash = pkg.substr(15 + tamEmisor + 100 + tamFileName + 18 + tamFile, 5);

        std::string archivo = pkg.substr(15 + tamEmisor + 100 + tamFileName + 18, tamFile);
        int hashLocal = calcularHash(archivo.c_str(), archivo.length());

        if (std::stoi(hash) == hashLocal)
            printf("\n[archivo] %s: %s (Hash OK)\n", emisor.c_str(), fileName.c_str());
        else
            printf("\n[archivo] %s: %s (Hash INCORRECTO: calculado %d, recibido %s)\n",
                         emisor.c_str(), fileName.c_str(), hashLocal, hash.c_str());

        if (std::stoi(hash) == hashLocal) {
            std::ofstream archivoSalida(fileName, std::ios::binary);
            if (archivoSalida.is_open()) {
                archivoSalida.write(archivo.c_str(), archivo.length());
                archivoSalida.close();
                std::cout << "Archivo " << fileName << " guardado.\n";
            } else {
                std::cerr << "Error al crear archivo.\n";
            }
        }
    }
    else if (tipo == 'x' || tipo == 'X') {
        puts("\nTABLERO");
        for (int j = 0; j < 9; ++j) {
            char c = pkg[10 + j];
            std::putchar(c);
            std::putchar((j % 3 == 2) ? '\n' : '|');
        }
    }
    else if (tipo == 't' || tipo == 'T') {
        simbolo = pkg[10];
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
        char codigo = pkg[10];
        int tamMsg = std::stoi(pkg.substr(11, 5));
        std::string mensaje = pkg.substr(16, tamMsg);
        std::cout << "\nERROR " << codigo << ": " << mensaje << "\n";
        if (codigo == '6') tuTurno = true;
    }
    else if (tipo == 'o' || tipo == 'O') {
        char res = pkg[10];
        if (!esEspectador) {
            if (res == 'W') puts("\n*** ¡Ganaste! ***");
            else if (res == 'L') puts("\n*** Perdiste ***");
            else puts("\n*** Empate ***");
        } else {
            puts("\n*** La partida termino ***");
        }
        enPartida = false;
        esEspectador = false;
    }
}

void leerSocket(int socketCliente) {
    char buffer[500];
    std::vector<std::string> vc;
    vc.clear();

    while (true) {
        int n = read(socketCliente, buffer, 500);
        if (n <= 0) break;

        std::string pkg(buffer, n);

        // Fragmentación (igual que en UDP)
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
            if (p.empty()) { completo = false; break; }

        if (completo) {
            // Reconstruir mensaje completo
            std::string mensaje;
            for (auto &p : vc) mensaje += p.substr(10);
            
            procesarMensaje(vc[0]); // Procesamos solo el primer paquete que contiene los metadatos
            vc.clear();
        }
    }
}

void enviarP(int sock, int pos, char simbolo) {
    char buffer[500];
    memset(buffer, '#', 500); // Corregido: removido std::
    int tamTot = 3;

    memcpy(buffer, "0001", 4); // Corregido: removido std::
    sprintf(buffer + 4, "%05d", tamTot);
    int off = 9;
    buffer[off++] = 'P';
    buffer[off++] = pos + '0';
    buffer[off++] = simbolo;

    write(sock, buffer, 500);
}

int main(void) {
    struct sockaddr_in direccion;
    int socketCliente = socket(PF_INET, SOCK_STREAM, 0);

    if (socketCliente == -1) {
        perror("socket");
        exit(1);
    }

    memset(&direccion, 0, sizeof(direccion));
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &direccion.sin_addr);

    if (connect(socketCliente, (const struct sockaddr *)&direccion, sizeof(direccion))) { // Corregido: añadido ')'
        perror("connect");
        exit(1);
    }

    socketGlobal = socketCliente;

    // Registro
    std::cout << "Ingresa tu nickname: ";
    std::getline(std::cin, nickname);
    
    char buffer[500];
    memset(buffer, '#', 500); // Corregido: removido std::
    sprintf(buffer, "0001%05dN%s\0", (int)nickname.length() + 1, nickname.c_str());
    write(socketCliente, buffer, 500);
    
    std::thread(leerSocket, socketCliente).detach();

    bool salir = false;
    while (!salir) {
        // Menu
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

        // Casilla de juego
        if (tuTurno && opcion >= '1' && opcion <= '9') {
            enviarP(socketCliente, opcion - '0', simbolo);
            tuTurno = false;
            continue;
        }

        // Respuesta para ver la partida
        if (preguntaVer) {
            if (opcion == 'y' || opcion == 'Y') {
                esEspectador = true;
                memset(buffer, '#', 500); // Corregido: removido std::
                memcpy(buffer, "0001", 4); // Corregido: removido std::
                sprintf(buffer + 4, "%05d", 1);
                int off = 9;
                buffer[off++] = 'V';
                write(socketCliente, buffer, 500);
            }
            else if (opcion == 'n' || opcion == 'N') {
                enPartida = false;
            }
            preguntaVer = false;
        }

        // Mensaje L
        else if (opcion == 'a') {
            memset(buffer, '#', 500); // Corregido: removido std::
            strcpy(buffer, "000100001L");
            write(socketCliente, buffer, 500);
            while (!envLista);
            envLista = false;
        }
        // Mensaje M
        else if (opcion == 'b') {
            std::string destino, mensaje;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\nNombre Destinatario: ";
            std::getline(std::cin, destino);
            std::cout << "\nMensaje: ";
            std::getline(std::cin, mensaje);

            int tamMsg = mensaje.length();
            int tamDest = destino.length();
            int sizeFijo = 20 + tamDest;
            int tamMax = 500 - sizeFijo;

            std::vector<std::string> piezas = (mensaje.length() > tamMax) ? 
                partir(mensaje, tamMax) : std::vector<std::string>{mensaje};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                memset(buffer, '#', 500); // Corregido: removido std::
                sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + trozo.length() + 5 + destino.length();

                int offset = 4;
                sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'M';

                sprintf(buffer + offset, "%05d", (int)trozo.length());
                offset += 5;
                memcpy(buffer + offset, trozo.c_str(), trozo.length()); // Corregido: removido std::
                offset += trozo.length();

                sprintf(buffer + offset, "%05d", (int)destino.length());
                offset += 5;
                memcpy(buffer + offset, destino.c_str(), destino.length()); // Corregido: removido std::

                write(socketCliente, buffer, 500);
                ++seq;
            }
        }
        // Mensaje Q
        else if (opcion == 'c') {
            salir = true;
            memset(buffer, '#', 500); // Corregido: removido std::
            strcpy(buffer, "000100001Q");
            write(socketCliente, buffer, 500);
        }
        // Mensaje B
        else if (opcion == 'd') {
            std::string mensaje;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\nMensaje: ";
            std::getline(std::cin, mensaje);

            int sizeFijo = 15;
            int tamMax = 500 - sizeFijo;

            std::vector<std::string> piezas = (mensaje.length() > tamMax) ? 
                partir(mensaje, tamMax) : std::vector<std::string>{mensaje};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                memset(buffer, '#', 500); // Corregido: removido std::
                sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + trozo.length();

                int offset = 4;
                sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'B';

                sprintf(buffer + offset, "%05d", (int)trozo.length());
                offset += 5;
                memcpy(buffer + offset, trozo.c_str(), trozo.length()); // Corregido: removido std::

                write(socketCliente, buffer, 500);
                ++seq;
            }
        }
        // Mensaje F
        else if (opcion == 'e') {
            std::string destino, rutaArchivo, nombreArchivo;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "\nNombre Destinatario: ";
            std::getline(std::cin, destino);
            std::cout << "Ruta del archivo a enviar: ";
            std::getline(std::cin, rutaArchivo);

            size_t posBarra = rutaArchivo.find_last_of("/\\");
            nombreArchivo = (posBarra != std::string::npos) ? 
                rutaArchivo.substr(posBarra + 1) : rutaArchivo;

            std::ifstream archivoEntrada(rutaArchivo, std::ios::binary);
            if (!archivoEntrada.is_open()) {
                std::cerr << "No se pudo abrir el archivo\n";
                continue;
            }
            std::vector<char> datosArchivo((std::istreambuf_iterator<char>(archivoEntrada)),
                std::istreambuf_iterator<char>());
            long tamanoArchivo = datosArchivo.size();
            archivoEntrada.close();

            int hash = calcularHash(datosArchivo.data(), tamanoArchivo);
            std::string archivo(datosArchivo.begin(), datosArchivo.end());

            int sizeFijo = 138 + destino.length() + nombreArchivo.length();
            int tamMax = 500 - sizeFijo;

            std::vector<std::string> piezas = (archivo.length() > tamMax) ? 
                partir(archivo, tamMax) : std::vector<std::string>{archivo};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                memset(buffer, '#', 500); // Corregido: removido std::
                sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + destino.length() + 100 + nombreArchivo.length() + 
                                 18 + trozo.length() + 5;

                int offset = 4;
                sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'F';

                sprintf(buffer + offset, "%05d", (int)destino.length());
                offset += 5;
                memcpy(buffer + offset, destino.c_str(), destino.length()); // Corregido: removido std::
                offset += destino.length();

                sprintf(buffer + offset, "%0100d", (int)nombreArchivo.length());
                offset += 100;
                memcpy(buffer + offset, nombreArchivo.c_str(), nombreArchivo.length()); // Corregido: removido std::
                offset += nombreArchivo.length();

                sprintf(buffer + offset, "%018d", (int)trozo.length());
                offset += 18;
                memcpy(buffer + offset, trozo.c_str(), trozo.length()); // Corregido: removido std::
                offset += trozo.length();

                memcpy(buffer + offset, std::to_string(hash).c_str(), 5); // Corregido: removido std::

                write(socketCliente, buffer, 500);
                ++seq;
            }
        }
        // Mensaje J
        else if (opcion == 'f') {
            if (!enPartida) {
                memset(buffer, '#', 500); // Corregido: removido std::
                strcpy(buffer, "000100001J");
                write(socketCliente, buffer, 500);
                enPartida = true;
            } else {
                std::cout << "Ya estas en partida.\n";
            }
        }
        else {
            std::cout << "Comando no valido.\n";
        }
    }

    close(socketCliente);
    return 0;
}