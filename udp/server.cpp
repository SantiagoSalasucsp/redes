/* Server code in C - TCP version with UDP-style protocol */

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
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <chrono> // Para std::chrono::milliseconds
#include <thread> // Para std::this_thread::sleep_for

using namespace std;

std::unordered_map<std::string, int> mapaSockets;
std::mutex mapaMutex;

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
std::mutex partidaMutex;

void enviarM(int sock, const string &msg);
void enviarX_aTodos();
void enviarT(const string &nick, char simbolo);
void finalizarPartida(char resultado);
bool ganador(char s);
bool tableroLleno();

// Nueva función para enviar ACK de archivo
void enviarACKArchivo(int sock, char resultado, const string& mensaje) {
    char buffer[500];
    memset(buffer, '#', 500);
    string res_msg = string(1, resultado) + mensaje;
    int totalSize = 1 + res_msg.length(); // 'A' + resultado + mensaje
    
    // Formato: 0000100001 (seq/tot) + 000xx (len) + 'A' (tipo) + 'S'/'F' (resultado) + mensaje
    sprintf(buffer, "0000100001%05dA%s", totalSize, res_msg.c_str());
    write(sock, buffer, 500);
    this_thread::sleep_for(chrono::milliseconds(50));
}


void procesarMensaje(int sock, const string &nick, const vector<string> &pkgs, char tipo) {
    char buffer[500];

    // Offset para el tipo de mensaje y datos cambia de 11 a 15 debido a 5 dígitos para seq y tot
    const int PROTOCOL_HEADER_LEN = 15; // 5 (seq) + 5 (tot) + 5 (len) + 1 (tipo)

    if (tipo == 'L') {
        string mensaje;
        string coma = "";
        
        {
            lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
            for (auto &par : mapaSockets) {
                if (par.first != nick) {
                    mensaje += coma + par.first;
                    coma = ",";
                }
            }
        }

        // Se usa 500 - (5 para seq + 5 para tot + 5 para len + 1 para tipo)
        int sizeFijo = 16; 
        int tamMax = 500 - sizeFijo;

        if (mensaje.length() > tamMax) {
            vector<string> piezas = partir(mensaje, tamMax);
            int seq = 1;
            int totalPkg = piezas.size();
            for (auto &trozo : piezas) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d%05dl%s",
                       (seq == totalPkg ? 0 : seq), totalPkg,
                       (int)trozo.length() + 1, trozo.c_str());
                write(sock, buffer, 500);
                this_thread::sleep_for(chrono::milliseconds(50));
                seq++;
            }
        } else {
            memset(buffer, '#', 500);
            sprintf(buffer, "0000100001%05dl%s", (int)mensaje.length() + 1, mensaje.c_str());
            write(sock, buffer, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    }
    else if (tipo == 'M') {
        // La lectura del paquete entrante también debe ajustarse al nuevo offset
        int tamMsgHeader = stoi(pkgs[0].substr(PROTOCOL_HEADER_LEN, 5));
        int tamDest = stoi(pkgs[0].substr(PROTOCOL_HEADER_LEN + 5 + tamMsgHeader, 5));
        string destino = pkgs[0].substr(PROTOCOL_HEADER_LEN + 5 + tamMsgHeader + 5, tamDest);

        string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            if (i == 0) {
                int t = stoi(pkgs[i].substr(PROTOCOL_HEADER_LEN, 5));
                mensaje += pkgs[i].substr(PROTOCOL_HEADER_LEN + 5, t);
            } else {
                mensaje += pkgs[i].substr(PROTOCOL_HEADER_LEN);
            }
        }

        lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
        if (mapaSockets.find(destino) != mapaSockets.end()) {
            int sizeFijo = 1 + 5 + 5 + nick.length(); // Tipo + TamMsg + TamRem
            int tamMaxPayload = 500 - (PROTOCOL_HEADER_LEN + sizeFijo -1); // -1 por el caracter de tipo que ya está contado
            
            vector<string> piezas = (mensaje.length() > tamMaxPayload) ? 
                partir(mensaje, tamMaxPayload) : vector<string>{mensaje};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // 5 dígitos para seq y tot
                int totalSize = 1 + 5 + (int)trozo.length() + 5 + (int)nick.length();

                int offset = 10; // Offset para len y tipo
                sprintf(buffer + offset, "%05d", totalSize);
                offset += 5;
                buffer[offset++] = 'm';

                sprintf(buffer + offset, "%05d", (int)trozo.length());
                offset += 5;
                memcpy(buffer + offset, trozo.c_str(), trozo.length());
                offset += trozo.length();

                sprintf(buffer + offset, "%05d", (int)nick.length());
                offset += 5;
                memcpy(buffer + offset, nick.c_str(), nick.length());

                write(mapaSockets[destino], buffer, 500);
                this_thread::sleep_for(chrono::milliseconds(50));
                seq++;
            }
        } else {
            enviarM(sock, "El usuario '" + destino + "' no esta conectado.");
        }
    }
    else if (tipo == 'B') {
        string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
             if (i == 0) {
                int t = stoi(pkgs[i].substr(PROTOCOL_HEADER_LEN, 5));
                mensaje += pkgs[i].substr(PROTOCOL_HEADER_LEN + 5, t);
            } else {
                mensaje += pkgs[i].substr(PROTOCOL_HEADER_LEN);
            }
        }

        int sizeFijo = 1 + 5 + 5 + nick.length();
        int tamMaxPayload = 500 - (PROTOCOL_HEADER_LEN + sizeFijo -1); // -1 por el caracter de tipo que ya está contado

        vector<string> piezas = (mensaje.length() > tamMaxPayload) ? 
            partir(mensaje, tamMaxPayload) : vector<string>{mensaje};

        int totalPkg = piezas.size();
        int seq = 1;

        for (auto &trozo : piezas) {
            memset(buffer, '#', 500);
            sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // 5 dígitos para seq y tot
            int totalSize = 1 + 5 + (int)trozo.length() + 5 + (int)nick.length();

            int offset = 10; // Offset para len y tipo
            sprintf(buffer + offset, "%05d", totalSize);
            offset += 5;
            buffer[offset++] = 'b';

            sprintf(buffer + offset, "%05d", (int)trozo.length());
            offset += 5;
            memcpy(buffer + offset, trozo.c_str(), trozo.length());
            offset += trozo.length();

            sprintf(buffer + offset, "%05d", (int)nick.length());
            offset += 5;
            memcpy(buffer + offset, nick.c_str(), nick.length());

            lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
            for (auto &par : mapaSockets) {
                if (par.first != nick) {
                    write(par.second, buffer, 500);
                    this_thread::sleep_for(chrono::milliseconds(50));
                }
            }
            seq++;
        }
    }
    else if (tipo == 'F') {
        // La lectura del paquete entrante también debe ajustarse al nuevo offset
        int tamRemitente = stoi(pkgs[0].substr(PROTOCOL_HEADER_LEN, 5));
        string remitente = pkgs[0].substr(PROTOCOL_HEADER_LEN + 5, tamRemitente);
        int tamFileName = stoi(pkgs[0].substr(PROTOCOL_HEADER_LEN + 5 + tamRemitente, 100)); // Usar 100 para tamaño del nombre
        string fileName = pkgs[0].substr(PROTOCOL_HEADER_LEN + 5 + tamRemitente + 100, tamFileName);
        
        size_t posLenPrimerTrozo = PROTOCOL_HEADER_LEN + 5 + tamRemitente + 100 + tamFileName;
        long lenPrimerTrozoActual = stol(pkgs[0].substr(posLenPrimerTrozo, 18));
        size_t posDatosPrimerPaquete = posLenPrimerTrozo + 18;
        string hashStr = pkgs[0].substr(posDatosPrimerPaquete + lenPrimerTrozoActual, 5);
        int hash = stoi(hashStr);

        string archivo;
        
        for (size_t i = 0; i < pkgs.size(); ++i) {
            if (i == 0) {
                archivo += pkgs[i].substr(posDatosPrimerPaquete, lenPrimerTrozoActual);
            } else {
                size_t posLenTrozoSubPaquete = PROTOCOL_HEADER_LEN; // El offset para subsiguientes
                long lenActualTrozo = stol(pkgs[i].substr(posLenTrozoSubPaquete, 18));
                size_t posDatosSubPaquete = posLenTrozoSubPaquete + 18;
                archivo += pkgs[i].substr(posDatosSubPaquete, lenActualTrozo);
            }
        }
        
        lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
        if (mapaSockets.find(nick) != mapaSockets.end()) { // 'nick' es el destino en este contexto, ya que lo recibió
            cout << "Servidor: Reenviando archivo '" << fileName << "' de '" << remitente << "' a '" << nick << "'." << endl;

            int headerPrimerFragmentoReenvio = 1 + 5 + remitente.length() + 100 + fileName.length() + 18 + 5;
            int tamMaxPrimerFragmentoReenvio = 500 - (PROTOCOL_HEADER_LEN + headerPrimerFragmentoReenvio -1);

            int headerSubFragmentoReenvio = 1 + 18 + 5;
            int tamMaxSubFragmentoReenvio = 500 - (PROTOCOL_HEADER_LEN + headerSubFragmentoReenvio -1);

            std::vector<std::string> piezas_archivo_para_reenvio;
            if (archivo.length() > tamMaxPrimerFragmentoReenvio) {
                piezas_archivo_para_reenvio.push_back(archivo.substr(0, tamMaxPrimerFragmentoReenvio));
                string resto_archivo = archivo.substr(tamMaxPrimerFragmentoReenvio);
                
                std::vector<std::string> subsiguientes = partir(resto_archivo, tamMaxSubFragmentoReenvio);
                piezas_archivo_para_reenvio.insert(piezas_archivo_para_reenvio.end(), subsiguientes.begin(), subsiguientes.end());
            } else {
                piezas_archivo_para_reenvio.push_back(archivo);
            }
            
            int totalPkg = piezas_archivo_para_reenvio.size();
            int seqReenvio = 1;

            bool reenvioExitoso = true;
            for (auto &trozo : piezas_archivo_para_reenvio) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", (seqReenvio == totalPkg ? 0 : seqReenvio), totalPkg); // 5 dígitos para seq y tot
                
                int totalSize;
                int offset = 10; // Offset para len y tipo
                
                if (seqReenvio == 1) {
                    totalSize = 1 + 5 + (int)remitente.length() + 100 + (int)fileName.length() +
                                18 + (int)trozo.length() + 5;
                    sprintf(buffer + offset, "%05d", totalSize);
                    offset += 5;
                    buffer[offset++] = 'f'; // Tipo 'f' para archivo
                    
                    sprintf(buffer + offset, "%05d", (int)remitente.length());
                    offset += 5;
                    memcpy(buffer + offset, remitente.c_str(), remitente.length());
                    offset += remitente.length();

                    sprintf(buffer + offset, "%0100d", (int)fileName.length());
                    offset += 100;
                    memcpy(buffer + offset, fileName.c_str(), fileName.length());
                    offset += fileName.length();
                    
                    sprintf(buffer + offset, "%018ld", trozo.length());
                    offset += 18;
                    memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();

                    sprintf(buffer + offset, "%05d", hash); // Hash como 5 dígitos
                } else {
                    totalSize = 1 + 18 + (int)trozo.length() + 5;
                    sprintf(buffer + offset, "%05d", totalSize);
                    offset += 5;
                    buffer[offset++] = 'f'; // Tipo 'f' para archivo

                    sprintf(buffer + offset, "%018ld", trozo.length());
                    offset += 18;
                    memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();

                    sprintf(buffer + offset, "%05d", hash); // Hash como 5 dígitos
                }
                if (write(mapaSockets[nick], buffer, 500) < 0) {
                    perror("write error durante reenvio de archivo");
                    reenvioExitoso = false;
                    break;
                }
                this_thread::sleep_for(chrono::milliseconds(50));
                seqReenvio++;
            }

            if (reenvioExitoso) {
                cout << "Servidor: Archivo '" << fileName << "' reenviado exitosamente a '" << nick << "'." << endl;
                enviarACKArchivo(sock, 'S', "Archivo '" + fileName + "' enviado exitosamente a '" + nick + "'.");
            } else {
                cerr << "Servidor: Fallo al reenviar archivo '" << fileName << "' a '" << nick << "'." << endl;
                enviarACKArchivo(sock, 'F', "Fallo al enviar archivo '" + fileName + "' a '" + nick + "'.");
            }

        } else {
            cerr << "Servidor: Destino '" << nick << "' no encontrado para reenviar archivo." << endl;
            enviarACKArchivo(sock, 'F', "Error: Destino '" + nick + "' no encontrado."); // 'sock' es el del remitente original
        }
    }
    else if (tipo == 'Q') {
        printf("\nEl cliente %s ha salido del chat\n", nick.c_str());
        lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
        mapaSockets.erase(nick);
        return;
    }
    else if (tipo == 'J') {
        lock_guard<mutex> lock(partidaMutex);
        if (!partida.activa && jugadorEnEspera.empty()) {
            jugadorEnEspera = nick;
            enviarM(sock, "wait for player");
        }
        else if (!partida.activa && !jugadorEnEspera.empty()) {
            partida.activa = true;
            partida.jugadorO = jugadorEnEspera;
            partida.jugadorX = nick;
            jugadorEnEspera = "";

            enviarM(mapaSockets[partida.jugadorO], "inicio");
            enviarM(mapaSockets[partida.jugadorX], "inicio");
            enviarX_aTodos();
            enviarT(partida.jugadorO, 'O');
        }
        else {
            enviarM(sock, "do you want to see?");
        }
    }
    else if (tipo == 'V') {
        lock_guard<mutex> lock(partidaMutex);
        if (partida.activa) {
            partida.espectadores.push_back(nick);
            char pkt[500];
            memset(pkt, '#', 500);
            memcpy(pkt, "0000100001", 10); // Ajuste a 5 dígitos para seq y tot
            memcpy(pkt + 10, "00010", 5);
            pkt[15] = 'X'; // Ajuste del offset para el tipo
            memcpy(pkt + 16, partida.tablero, 9); // Ajuste del offset para los datos
            write(sock, pkt, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
        } else {
            enviarM(sock, "La partida no esta activa para ver.");
        }
    }
    else if (tipo == 'P') {
        char pos = pkgs[0][PROTOCOL_HEADER_LEN]; // Ajuste del offset
        char simb = pkgs[0][PROTOCOL_HEADER_LEN + 1]; // Ajuste del offset

        lock_guard<mutex> lock(partidaMutex);
        if (!((nick == partida.jugadorO && simb == 'O' && partida.turno == 'O') ||
              (nick == partida.jugadorX && simb == 'X' && partida.turno == 'X'))) {
            char errPkt[500];
            memset(errPkt, '#', 500);
            memcpy(errPkt, "0000100001", 10); // Ajuste a 5 dígitos para seq y tot
            memcpy(errPkt + 10, "00021", 5); // Offset ajustado
            int off = 15; // Offset ajustado
            errPkt[off++] = 'E';
            errPkt[off++] = '7';
            memcpy(errPkt + off, "00014", 5);
            off += 5;
            memcpy(errPkt + off, "No es tu turno", 14);
            write(sock, errPkt, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
            return;
        }

        int idx = pos - '1';
        if (idx < 0 || idx > 8 || partida.tablero[idx] != '_') {
            char errPkt[500];
            memset(errPkt, '#', 500);
            memcpy(errPkt, "0000100001", 10); // Ajuste a 5 dígitos para seq y tot
            memcpy(errPkt + 10, "00024", 5); // Offset ajustado
            int off = 15; // Offset ajustado
            errPkt[off++] = 'E';
            errPkt[off++] = '6';
            memcpy(errPkt + off, "00016", 5);
            off += 5;
            memcpy(errPkt + off, "Posicion ocupada", 16);
            write(sock, errPkt, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
            return;
        }

        partida.tablero[idx] = simb;

        if (ganador(simb)) {
            enviarX_aTodos();
            this_thread::sleep_for(chrono::milliseconds(50));
            finalizarPartida(simb);
        }
        else if (tableroLleno()) {
            enviarX_aTodos();
            finalizarPartida('E');
        }
        else {
            enviarX_aTodos();
            partida.turno = (partida.turno == 'O') ? 'X' : 'O';
            const string &prox = (partida.turno == 'O') ? partida.jugadorO : partida.jugadorX;
            enviarT(prox, partida.turno);
        }
    }
}

void enviarM(int sock, const string &msg) {
    const string remitente = "servidor";
    int lenMsg = msg.size();
    int lenRem = remitente.size();
    int tamTot = 1 + 5 + lenMsg + 5 + lenRem;

    char buffer[500];
    memset(buffer, '#', 500);
    memcpy(buffer, "0000100001", 10); // 5 dígitos para seq y tot
    sprintf(buffer + 10, "%05d", tamTot); // Offset ajustado
    int off = 15; // Offset ajustado
    buffer[off++] = 'm';
    sprintf(buffer + off, "%05d", lenMsg);
    off += 5;
    memcpy(buffer + off, msg.c_str(), lenMsg);
    off += lenMsg;
    sprintf(buffer + off, "%05d", lenRem);
    off += 5;
    memcpy(buffer + off, remitente.c_str(), lenRem);
    write(sock, buffer, 500);
    this_thread::sleep_for(chrono::milliseconds(50));
}

void enviarX_aTodos() {
    char pkt[500];
    memset(pkt, '#', 500);
    memcpy(pkt, "0000100001", 10); // 5 dígitos para seq y tot
    memcpy(pkt + 10, "00010", 5); // Offset ajustado
    pkt[15] = 'X'; // Offset ajustado
    memcpy(pkt + 16, partida.tablero, 9); // Offset ajustado

    lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
    if (!partida.jugadorO.empty() && mapaSockets.count(partida.jugadorO))
        write(mapaSockets[partida.jugadorO], pkt, 500);
    if (!partida.jugadorX.empty() && mapaSockets.count(partida.jugadorX))
        write(mapaSockets[partida.jugadorX], pkt, 500);
    for (auto &esp : partida.espectadores)
        if (mapaSockets.count(esp))
            write(mapaSockets[esp], pkt, 500);
    this_thread::sleep_for(chrono::milliseconds(50));
}

void enviarT(const string &nick, char simbolo) {
    char pkt[500];
    memset(pkt, '#', 500);
    memcpy(pkt, "0000100001", 10); // 5 dígitos para seq y tot
    memcpy(pkt + 10, "00002", 5); // Offset ajustado
    pkt[15] = 'T'; // Offset ajustado
    pkt[16] = simbolo; // Offset ajustado
    lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
    if (mapaSockets.count(nick))
        write(mapaSockets[nick], pkt, 500);
    this_thread::sleep_for(chrono::milliseconds(50));
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

void finalizarPartida(char resultado) {
    char pktO[500], pktX[500], pktE[500];
    
    memset(pktO, '#', 500);
    memcpy(pktO, "0000100001", 10); // 5 dígitos para seq y tot
    memcpy(pktO + 10, "00002", 5); // Offset ajustado
    pktO[15] = 'O'; // Offset ajustado
    pktO[16] = (resultado == 'O') ? 'W' : (resultado == 'X') ? 'L' : 'E'; // Offset ajustado
    
    memset(pktX, '#', 500);
    memcpy(pktX, "0000100001", 10); // 5 dígitos para seq y tot
    memcpy(pktX + 10, "00002", 5); // Offset ajustado
    pktX[15] = 'O'; // Offset ajustado
    pktX[16] = (resultado == 'X') ? 'W' : (resultado == 'O') ? 'L' : 'E'; // Offset ajustado
    
    memset(pktE, '#', 500);
    memcpy(pktE, "0000100001", 10); // 5 dígitos para seq y tot
    memcpy(pktE + 10, "00002", 5); // Offset ajustado
    pktE[15] = 'O'; // Offset ajustado
    pktE[16] = 'E'; // Offset ajustado

    lock_guard<mutex> lock1(mapaMutex); // Corrected: use mapaMutex
    lock_guard<mutex> lock2(partidaMutex);
    
    if (!partida.jugadorO.empty() && mapaSockets.count(partida.jugadorO))
        write(mapaSockets[partida.jugadorO], pktO, 500);
    if (!partida.jugadorX.empty() && mapaSockets.count(partida.jugadorX))
        write(mapaSockets[partida.jugadorX], pktX, 500);
    for (const string& esp : partida.espectadores) {
        if (mapaSockets.count(esp)) {
            write(mapaSockets[esp], pktE, 500);
        }
    }
    this_thread::sleep_for(chrono::milliseconds(50));

    partida = Partida();
    
    if (!jugadorEnEspera.empty() && mapaSockets.count(jugadorEnEspera)) {
        enviarM(mapaSockets[jugadorEnEspera], "La partida a la que ibas a unirte termino o fue cancelada.");
    }
    jugadorEnEspera = "";
}

void manejarCliente(int sock, const string &nick) {
    char buffer[500];
    vector<string> vc;
    int expected_total_pkgs = 0;
    int current_message_number = 0; // Para el terminal del server

    while (true) {
        int n = read(sock, buffer, 500);
        if (n <= 0) {
            if (n == 0) {
                printf("\nCliente %s desconectado.\n", nick.c_str());
            } else {
                perror("read error");
            }
            break;
        }

        string pkg(buffer, n);

        if (pkg.length() < 10) { // Mínimo 10 para seq y tot (5+5)
            cerr << "Error: Paquete demasiado corto para cabecera de fragmentacion del cliente " << nick << endl;
            continue;
        }

        int seq = stoi(pkg.substr(0, 5)); // Ahora 5 dígitos
        int tot = stoi(pkg.substr(5, 5)); // Ahora 5 dígitos

        if (seq < 0 || seq > tot || tot <= 0) {
            cerr << "Error de fragmento: Valores de secuencia/total invalidos (seq=" << seq << ", tot=" << tot << ") del cliente " << nick << ". Reiniciando reensamblaje." << endl;
            vc.clear();
            expected_total_pkgs = 0;
            continue;
        }

        int idx = (seq == 0) ? tot - 1 : seq - 1; // 0 significa el último paquete, su índice es tot-1

        if (expected_total_pkgs == 0) {
            expected_total_pkgs = tot;
            vc.resize(tot);
            current_message_number++; // Nuevo mensaje empezando
            cout << "Recibiendo mensaje #" << current_message_number << " del cliente " << nick << endl;
        } else if (tot != expected_total_pkgs) {
            cerr << "Error de fragmento: El numero total de paquetes cambio (esperado "
                 << expected_total_pkgs << ", recibido " << tot << ") del cliente " << nick << ". Reiniciando reensamblaje." << endl;
            vc.clear();
            expected_total_pkgs = tot;
            vc.resize(tot);
            current_message_number++; // Considerar como nuevo mensaje
            cout << "Recibiendo mensaje #" << current_message_number << " del cliente " << nick << endl;
        }

        if (idx < 0 || idx >= vc.size()) {
            cerr << "Error de fragmento: idx fuera de rango o invalido (idx=" << idx << ", vc.size=" << vc.size() << ") del cliente " << nick << "." << endl;
            continue;
        }

        if (!vc[idx].empty()) {
            cerr << "Advertencia: Fragmento " << idx + 1 << " ya recibido para mensaje #" << current_message_number << " del cliente " << nick << ". Ignorando duplicado o error." << endl;
            continue;
        }

        vc[idx] = move(pkg);
        cout << "Recibiendo fragmento " << idx + 1 << " de " << expected_total_pkgs << " del mensaje #" << current_message_number << " del cliente " << nick << endl;


        bool completo = true;
        for (int i = 0; i < expected_total_pkgs; ++i) {
            if (vc[i].empty()) {
                completo = false;
                break;
            }
        }

        if (completo) {
            // El tipo de mensaje está en pkg[15] ahora (5+5+5)
            char tipo = vc[0][15]; 
            vector<string> paquetesCompletos = move(vc);
            vc.clear();
            expected_total_pkgs = 0;

            procesarMensaje(sock, nick, paquetesCompletos, tipo);
        }
    }

    {
        lock_guard<mutex> lock1(mapaMutex); // Corrected: use mapaMutex
        lock_guard<mutex> lock2(partidaMutex);
        
        if (mapaSockets.count(nick)) {
            printf("\nEl cliente %s ha salido del chat (conexion terminada).\n", nick.c_str());
            mapaSockets.erase(nick);

            if (partida.activa) {
                if (partida.jugadorO == nick) {
                    if (mapaSockets.count(partida.jugadorX))
                        enviarM(mapaSockets[partida.jugadorX], "Tu oponente se ha desconectado. Has ganado por abandono.");
                    finalizarPartida('W');
                } else if (partida.jugadorX == nick) {
                    if (mapaSockets.count(partida.jugadorO))
                        enviarM(mapaSockets[partida.jugadorO], "Tu oponente se ha desconectado. Has ganado por abandono.");
                    finalizarPartida('W');
                }
                partida.espectadores.erase(std::remove(partida.espectadores.begin(), partida.espectadores.end(), nick), partida.espectadores.end());
            }
            if (jugadorEnEspera == nick) {
                jugadorEnEspera = "";
            }
        }
    }
    close(sock);
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(1);
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(45000);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr))) {
        perror("bind");
        exit(1);
    }

    if (listen(sock, 10)) {
        perror("listen");
        exit(1);
    }

    cout << "Servidor iniciado en puerto 45000..." << endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(sock, (sockaddr*)&client_addr, &addr_len);
        if (client_sock == -1) {
            perror("accept");
            continue;
        }

        char buffer[500];
        int n = read(client_sock, buffer, 500);
        if (n <= 0) {
            if (n == 0) {
                cout << "Nuevo cliente se desconecto inmediatamente." << endl;
            } else {
                perror("read initial registration");
            }
            close(client_sock);
            continue;
        }

        string pkg(buffer, n);
        // La validación del paquete de registro N también debe ajustarse a 5 dígitos para seq y tot
        // y el offset del tipo 'N'.
        // "0000100001" (seq + tot, 10 caracteres) + "00001" (len, 5 caracteres) + 'N' (1 caracter) = 16
        if (pkg.length() >= 16 && pkg.substr(0,10) == "0000100001" && pkg[15] == 'N') { // Ajuste a 5 dígitos para seq y tot, y offset para 'N'
            int tamNick = stoi(pkg.substr(10, 5)) - 1; // Offset ajustado (longitud incluye 'N')
            if (tamNick > 0 && (size_t)(16 + tamNick) <= pkg.length()) { // Offset ajustado
                string nick = pkg.substr(16, tamNick); // Offset ajustado
                
                lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
                if (mapaSockets.find(nick) == mapaSockets.end()) {
                    mapaSockets[nick] = client_sock;
                    cout << "Cliente conectado: " << nick << endl;
                    thread(manejarCliente, client_sock, nick).detach();
                } else {
                    enviarM(client_sock, "Error: Nickname ya en uso. Por favor, elige otro.");
                    close(client_sock);
                    cout << "Cliente rechazado: Nickname '" << nick << "' ya en uso." << endl;
                }
            } else {
                cerr << "Error: Paquete de registro N malformado o longitud de nickname invalida." << endl;
                close(client_sock);
            }
        } else {
            cerr << "Error: Primer paquete no es de registro N o esta malformado." << endl;
            close(client_sock);
        }
    }

    close(sock);
    return 0;
}