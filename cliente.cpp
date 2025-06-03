/* Client code in C */

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
#include <chrono>
#include <string>
#include <atomic>
#include <limits>
#include <fstream>
#include <vector>

std::string nickname;
std::atomic<bool> envLista(false);
std::atomic<bool> enPartida(false);
std::atomic<bool> esEspectador(false);
std::atomic<char> simbolo{'\0'};
int socketGlobal;

void pedirYEnviarP(int sock, char simbolo);

int calcularHash(const char *datos, long tamano) {
    int hash = 0;
    for (long i = 0; i < tamano; i++) {
        hash = (hash + (unsigned char)datos[i]) % 100000;
    }
    return hash;
}

void leerSocket(int socketCliente) {
    char *buffer = (char *)malloc(10);
    int n, tamano;
    do {
        n = read(socketCliente, buffer, 5);
        if (n <= 0) break;
        buffer[n] = '\0';
        tamano = atoi(buffer);

        buffer = (char *)realloc(buffer, tamano + 1);
        n = read(socketCliente, buffer, 1);
        if (n <= 0) break;
        buffer[n] = '\0';

        // Mensaje l - Lista de usuarios
        if (buffer[0] == 'l') {
            n = read(socketCliente, buffer, tamano - 1);
            buffer[n] = '\0';
            printf("\nUsuarios conectados: %s\n", buffer);
            envLista = true;
        }
        // Mensaje m - Mensaje privado
        else if (buffer[0] == 'm') {
            std::string usuarioOrigen, mensaje;
            n = read(socketCliente, buffer, 5);
            buffer[n] = '\0';
            int tamanoMensaje = atoi(buffer);

            n = read(socketCliente, buffer, tamanoMensaje);
            buffer[n] = '\0';
            mensaje = buffer;

            n = read(socketCliente, buffer, 5);
            buffer[n] = '\0';
            int tamanoUsuario = atoi(buffer);

            n = read(socketCliente, buffer, tamanoUsuario);
            buffer[n] = '\0';
            usuarioOrigen = buffer;

            printf("\n\n%s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());

            if (usuarioOrigen == "Servidor" || usuarioOrigen == "servidor") {
                if (mensaje.find("do you want to see") != std::string::npos) {
                    int opcion;
                    std::cout << "\n¿Deseas ver la partida?\n1. Sí\n2. No\n> ";
                    std::cin >> opcion;

                    if (opcion == 1) {
                        esEspectador = true;
                        write(socketCliente, "00001V", 6);
                    } else {
                        enPartida = false;
                    }
                }
            }
        }
        // Mensaje b - Broadcast
        else if (buffer[0] == 'b') {
            std::string usuarioOrigen, mensaje;
            n = read(socketCliente, buffer, 5);
            buffer[n] = '\0';
            int tamanoMensaje = atoi(buffer);

            n = read(socketCliente, buffer, tamanoMensaje);
            buffer[n] = '\0';
            mensaje = buffer;

            n = read(socketCliente, buffer, 5);
            buffer[n] = '\0';
            int tamanoUsuario = atoi(buffer);

            n = read(socketCliente, buffer, tamanoUsuario);
            buffer[n] = '\0';
            usuarioOrigen = buffer;

            printf("\n\n[broadcast] %s: %s\n", usuarioOrigen.c_str(), mensaje.c_str());
        }
        // Mensaje f - Archivo
        else if (buffer[0] == 'f') {
            std::string emisorArchivo, nombreArchivo;
            char hashRecibido[6];

            n = read(socketCliente, buffer, 5);
            buffer[n] = '\0';
            int tamanoEmisor = atoi(buffer);

            n = read(socketCliente, buffer, tamanoEmisor);
            buffer[n] = '\0';
            emisorArchivo = buffer;

            n = read(socketCliente, buffer, 100);
            buffer[100] = '\0';
            long tamanoNombreArchivo = strtol(buffer, NULL, 10);

            char *nombreArchivoC = new char[tamanoNombreArchivo + 1];
            n = read(socketCliente, nombreArchivoC, tamanoNombreArchivo);
            nombreArchivoC[n] = '\0';
            nombreArchivo = std::string(nombreArchivoC, tamanoNombreArchivo);
            delete[] nombreArchivoC;

            n = read(socketCliente, buffer, 18);
            buffer[18] = '\0';
            long tamanoArchivo = atol(buffer);

            char *contenidoArchivo = new char[tamanoArchivo + 1];
            long totalLeido = 0;
            while (totalLeido < tamanoArchivo) {
                n = read(socketCliente, contenidoArchivo + totalLeido, tamanoArchivo - totalLeido);
                if (n <= 0) break;
                totalLeido += n;
            }

            n = read(socketCliente, hashRecibido, 5);
            hashRecibido[5] = '\0';

            int hashLocal = calcularHash(contenidoArchivo, tamanoArchivo);
            char hashCalculado[6];
            sprintf(hashCalculado, "%05d", hashLocal);

            if (strcmp(hashCalculado, hashRecibido) == 0) {
                printf("\n[archivo] %s: %s (Hash OK)\n", emisorArchivo.c_str(), nombreArchivo.c_str());
                
                std::ofstream archivoSalida(nombreArchivo, std::ios::binary);
                if (archivoSalida.is_open()) {
                    archivoSalida.write(contenidoArchivo, tamanoArchivo);
                    archivoSalida.close();
                    std::cout << "Archivo guardado como: " << nombreArchivo << std::endl;
                } else {
                    std::cerr << "Error al guardar el archivo." << std::endl;
                }
            } else {
                printf("\n[archivo] %s: %s (Hash INCORRECTO)\n", emisorArchivo.c_str(), nombreArchivo.c_str());
            }

            delete[] contenidoArchivo;
        }
        // Mensaje X - Tablero de juego
        else if (buffer[0] == 'X') {
            n = read(socketCliente, buffer, 9);
            buffer[9] = '\0';
            printf("\nTABLERO\n");
            for (int i = 0; i < 9; i++) {
                printf("%c", buffer[i]);
                if (i % 3 == 2) printf("\n");
                else printf("|");
            }
        }
        // Mensaje T - Turno de juego
        else if (buffer[0] == 'T') {
            read(socketCliente, &simbolo, 1);
            pedirYEnviarP(socketCliente, simbolo);
        }
        // Mensaje E - Error
        else if (buffer[0] == 'E') {
            char codigo;
            read(socketCliente, &codigo, 1);
            char len[6];
            read(socketCliente, len, 5);
            int tam = atoi(len);
            buffer = (char *)realloc(buffer, tam + 1);
            n = read(socketCliente, buffer, tam);
            buffer[n] = '\0';
            std::cout << "\nERROR " << codigo << ": " << buffer << "\n";
            if (codigo == '6') {
                pedirYEnviarP(socketCliente, simbolo);
            }
        }
        // Mensaje O - Resultado del juego
        else if (buffer[0] == 'O') {
            char res;
            read(socketCliente, &res, 1);
            if (!esEspectador) {
                if (res == 'W') puts("\n*** ¡Ganaste! ***");
                else if (res == 'L') puts("\n*** Perdiste ***");
                else puts("\n*** Empate ***");
            } else {
                puts("\n*** La partida terminó ***");
            }
            enPartida = false;
            esEspectador = false;
        }

    } while (true);
    free(buffer);
}

void pedirYEnviarP(int sock, char simbolo) {
    int opcion;
    std::cout << "\nTu turno (" << simbolo << "). Opciones:\n";
    std::cout << "1. Ver lista de usuarios\n";
    std::cout << "2. Enviar mensaje privado\n";
    std::cout << "3. Enviar mensaje broadcast\n";
    std::cout << "4. Enviar archivo\n";
    std::cout << "5. Salir del juego\n";
    std::cout << "6-9. Jugada (1-9)\n";
    std::cout << "Seleccione una opción: ";
    std::cin >> opcion;

    switch(opcion) {
        case 1: {
            char buffer[7] = "00001L";
            write(sock, buffer, 6);
            while (!envLista);
            envLista = false;
            break;
        }
        case 2: {
            std::string destino, mensaje;
            std::cin.ignore();
            std::cout << "Destinatario: ";
            std::getline(std::cin, destino);
            std::cout << "Mensaje: ";
            std::getline(std::cin, mensaje);
            
            char* buffer = new char[destino.length() + mensaje.length() + 16];
            sprintf(buffer, "%05dM%05d%s%05d%s", 
                   (int)(destino.length() + mensaje.length() + 11),
                   (int)mensaje.length(), mensaje.c_str(),
                   (int)destino.length(), destino.c_str());
            write(sock, buffer, destino.length() + mensaje.length() + 16);
            delete[] buffer;
            break;
        }
        case 3: {
            std::string mensaje;
            std::cin.ignore();
            std::cout << "Mensaje: ";
            std::getline(std::cin, mensaje);
            
            char* buffer = new char[mensaje.length() + 11];
            sprintf(buffer, "%05dB%05d%s", 
                   (int)(mensaje.length() + 6), 
                   (int)mensaje.length(), mensaje.c_str());
            write(sock, buffer, mensaje.length() + 11);
            delete[] buffer;
            break;
        }
        case 4: {
            std::string destino, rutaArchivo;
            std::cin.ignore();
            std::cout << "Destinatario: ";
            std::getline(std::cin, destino);
            std::cout << "Ruta del archivo: ";
            std::getline(std::cin, rutaArchivo);

            size_t pos = rutaArchivo.find_last_of("/\\");
            std::string nombreArchivo = (pos == std::string::npos) ? rutaArchivo : rutaArchivo.substr(pos + 1);

            std::ifstream archivo(rutaArchivo, std::ios::binary | std::ios::ate);
            if (!archivo.is_open()) {
                std::cerr << "Error al abrir el archivo." << std::endl;
                break;
            }
            long tamanoArchivo = archivo.tellg();
            archivo.seekg(0, std::ios::beg);
            
            char* contenido = new char[tamanoArchivo];
            archivo.read(contenido, tamanoArchivo);
            archivo.close();

            int hash = calcularHash(contenido, tamanoArchivo);
            char hashStr[6];
            sprintf(hashStr, "%05d", hash);

            // Enviar metadatos por TCP
            char* buffer = new char[1 + 5 + destino.length() + 100 + 18 + 5 + nombreArchivo.length() + 5];
            sprintf(buffer, "%05ldF%05ld%s%0100ld%s%018ld%s",
                   1L + 5L + destino.length() + 100L + nombreArchivo.length() + 18L + 5L,
                   (long)destino.length(), destino.c_str(),
                   (long)nombreArchivo.length(), nombreArchivo.c_str(),
                   tamanoArchivo, hashStr);
            write(sock, buffer, 1 + 5 + destino.length() + 100 + 18 + 5 + nombreArchivo.length());
            delete[] buffer;

            // Crear socket UDP para enviar el archivo
            int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
            if (udp_socket == -1) {
                perror("Error al crear socket UDP");
                delete[] contenido;
                break;
            }

            struct sockaddr_in serverAddr;
            memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(45001); // Puerto UDP del servidor
            inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

            // Fragmentar y enviar por UDP
            const int payload = 485;
            int total_fragments = (tamanoArchivo + payload - 1) / payload;
            for (int i = 0; i < total_fragments; ++i) {
                int offset = i * payload;
                int tam_frag = std::min(payload, (int)(tamanoArchivo - offset));

                char paquete[500];
                memset(paquete, '#', 500);

                char seq_str[6], tot_str[6], size_str[6];
                sprintf(seq_str, "%05d", (i == total_fragments - 1) ? 0 : i + 1);
                sprintf(tot_str, "%05d", total_fragments);
                sprintf(size_str, "%05d", tam_frag);

                memcpy(paquete,        seq_str, 5);
                memcpy(paquete + 5,    tot_str, 5);
                memcpy(paquete + 10,   size_str, 5);
                memcpy(paquete + 15,   contenido + offset, tam_frag);

                sendto(udp_socket, paquete, 500, 0,
                       (struct sockaddr *)&serverAddr, sizeof(serverAddr));
                usleep(20000); // 20ms
            }

            close(udp_socket);
            delete[] contenido;
            break;
        }
        case 5: {
            char buffer[7] = "00001Q";
            write(sock, buffer, 6);
            enPartida = false;
            break;
        }
        case 6: case 7: case 8: case 9: {
            char pkt[9];
            sprintf(pkt, "00003P%c%c", char('0' + opcion), simbolo);
            write(sock, pkt, 8);
            break;
        }
        default:
            std::cout << "Opción inválida. Intente de nuevo.\n";
            break;
    }
}

int main(void) {
    struct sockaddr_in direccion;
    int socketCliente = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (socketCliente == -1) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    memset(&direccion, 0, sizeof(struct sockaddr_in));
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &direccion.sin_addr);

    if (connect(socketCliente, (const struct sockaddr *)&direccion, sizeof(struct sockaddr_in)) == -1) {
        perror("Error en connect");
        close(socketCliente);
        exit(EXIT_FAILURE);
    }

    socketGlobal = socketCliente;
    
    std::cout << "Ingresa tu nombre: ";
    std::getline(std::cin, nickname);
    char *buffer = new char[nickname.length() + 7];
    sprintf(buffer, "%05dN%s", (int)nickname.length() + 1, nickname.c_str());
    write(socketCliente, buffer, nickname.length() + 6);
    delete[] buffer;

    std::thread(leerSocket, socketCliente).detach();

    while (true) {
        int opcion;
        std::cout << "\nMENU PRINCIPAL\n";
        std::cout << "1. Ver lista de usuarios\n";
        std::cout << "2. Enviar mensaje privado\n";
        std::cout << "3. Enviar mensaje broadcast\n";
        std::cout << "4. Enviar archivo\n";
        std::cout << "5. Unirse al juego\n";
        std::cout << "6. Salir\n";
        std::cout << "Seleccione una opción: ";
        std::cin >> opcion;

        switch(opcion) {
            case 1: {
                char buffer[7] = "00001L";
                write(socketCliente, buffer, 6);
                while (!envLista);
                envLista = false;
                break;
            }
            case 2: {
                std::string destino, mensaje;
                std::cin.ignore();
                std::cout << "Destinatario: ";
                std::getline(std::cin, destino);
                std::cout << "Mensaje: ";
                std::getline(std::cin, mensaje);
                
                buffer = new char[destino.length() + mensaje.length() + 16];
                sprintf(buffer, "%05dM%05d%s%05d%s", 
                       (int)(destino.length() + mensaje.length() + 11),
                       (int)mensaje.length(), mensaje.c_str(),
                       (int)destino.length(), destino.c_str());
                write(socketCliente, buffer, destino.length() + mensaje.length() + 16);
                delete[] buffer;
                break;
            }
            case 3: {
                std::string mensaje;
                std::cin.ignore();
                std::cout << "Mensaje: ";
                std::getline(std::cin, mensaje);
                
                buffer = new char[mensaje.length() + 11];
                sprintf(buffer, "%05dB%05d%s", 
                       (int)(mensaje.length() + 6), 
                       (int)mensaje.length(), mensaje.c_str());
                write(socketCliente, buffer, mensaje.length() + 11);
                delete[] buffer;
                break;
            }
            case 4: {
                std::string destino, rutaArchivo;
                std::cin.ignore();
                std::cout << "Destinatario: ";
                std::getline(std::cin, destino);
                std::cout << "Ruta del archivo: ";
                std::getline(std::cin, rutaArchivo);

                size_t pos = rutaArchivo.find_last_of("/\\");
                std::string nombreArchivo = (pos == std::string::npos) ? rutaArchivo : rutaArchivo.substr(pos + 1);

                std::ifstream archivo(rutaArchivo, std::ios::binary | std::ios::ate);
                if (!archivo.is_open()) {
                    std::cerr << "Error al abrir el archivo." << std::endl;
                    break;
                }
                long tamanoArchivo = archivo.tellg();
                archivo.seekg(0, std::ios::beg);
                
                char* contenido = new char[tamanoArchivo];
                archivo.read(contenido, tamanoArchivo);
                archivo.close();

                int hash = calcularHash(contenido, tamanoArchivo);
                char hashStr[6];
                sprintf(hashStr, "%05d", hash);

                // Enviar metadatos por TCP
                buffer = new char[1 + 5 + destino.length() + 100 + 18 + 5 + nombreArchivo.length() + 5];
                sprintf(buffer, "%05ldF%05ld%s%0100ld%s%018ld%s",
                       1L + 5L + destino.length() + 100L + nombreArchivo.length() + 18L + 5L,
                       (long)destino.length(), destino.c_str(),
                       (long)nombreArchivo.length(), nombreArchivo.c_str(),
                       tamanoArchivo, hashStr);
                write(socketCliente, buffer, 1 + 5 + destino.length() + 100 + 18 + 5 + nombreArchivo.length());
                delete[] buffer;

                // Crear socket UDP para enviar el archivo
                int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
                if (udp_socket == -1) {
                    perror("Error al crear socket UDP");
                    delete[] contenido;
                    break;
                }

                struct sockaddr_in serverAddr;
                memset(&serverAddr, 0, sizeof(serverAddr));
                serverAddr.sin_family = AF_INET;
                serverAddr.sin_port = htons(45001); // Puerto UDP del servidor
                inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

                // Fragmentar y enviar por UDP
                const int payload = 485;
                int total_fragments = (tamanoArchivo + payload - 1) / payload;
                for (int i = 0; i < total_fragments; ++i) {
                    int offset = i * payload;
                    int tam_frag = std::min(payload, (int)(tamanoArchivo - offset));

                    char paquete[500];
                    memset(paquete, '#', 500);

                    char seq_str[6], tot_str[6], size_str[6];
                    sprintf(seq_str, "%05d", (i == total_fragments - 1) ? 0 : i + 1);
                    sprintf(tot_str, "%05d", total_fragments);
                    sprintf(size_str, "%05d", tam_frag);

                    memcpy(paquete,        seq_str, 5);
                    memcpy(paquete + 5,    tot_str, 5);
                    memcpy(paquete + 10,   size_str, 5);
                    memcpy(paquete + 15,   contenido + offset, tam_frag);

                    sendto(udp_socket, paquete, 500, 0,
                           (struct sockaddr *)&serverAddr, sizeof(serverAddr));
                    usleep(20000); // 20ms
                }

                close(udp_socket);
                delete[] contenido;
                break;
            }
            case 5: {
                char buffer[7] = "00001J";
                write(socketCliente, buffer, 6);
                enPartida = true;
                while (enPartida);
                break;
            }
            case 6: {
                char buffer[7] = "00001Q";
                write(socketCliente, buffer, 6);
                close(socketCliente);
                return 0;
            }
            default:
                std::cout << "Opción inválida. Intente de nuevo.\n";
                break;
        }
    }

    close(socketCliente);
    return 0;
}
