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

void procesarMensaje(int sock, const string &nick, const vector<string> &pkgs, char tipo) {
    char buffer[500];

    if (tipo == 'L') {
        string mensaje;
        string coma = "";
        
        {
            lock_guard<mutex> lock(mapaMutex);
            for (auto &par : mapaSockets) {
                if (par.first != nick) {
                    mensaje += coma + par.first;
                    coma = ",";
                }
            }
        }

        int sizeFijo = 10;
        int tamMax = 500 - sizeFijo;

        if (mensaje.length() > tamMax) {
            vector<string> piezas = partir(mensaje, tamMax);
            int seq = 1;
            int totalPkg = piezas.size();
            for (auto &trozo : piezas) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%02d%02d%05dl%s",
                       (seq == totalPkg ? 0 : seq), totalPkg,
                       (int)trozo.length() + 1, trozo.c_str());
                write(sock, buffer, 500);
                seq++;
            }
        } else {
            memset(buffer, '#', 500);
            sprintf(buffer, "0001%05dl%s", (int)mensaje.length() + 1, mensaje.c_str());
            write(sock, buffer, 500);
        }
    }
    else if (tipo == 'M') {
        int tamMsgHeader = stoi(pkgs[0].substr(10, 5));
        int tamDest = stoi(pkgs[0].substr(15 + tamMsgHeader, 5));
        string destino = pkgs[0].substr(15 + tamMsgHeader + 5, tamDest);

        string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            if (i == 0) {
                int t = stoi(pkgs[i].substr(10, 5));
                mensaje += pkgs[i].substr(15, t);
            } else {
                mensaje += pkgs[i].substr(10);
            }
        }

        lock_guard<mutex> lock(mapaMutex);
        if (mapaSockets.find(destino) != mapaSockets.end()) {
            int sizeFijo = 1 + 5 + 5 + nick.length();
            int tamMaxPayload = 500 - (10 + sizeFijo);
            
            vector<string> piezas = (mensaje.length() > tamMaxPayload) ? 
                partir(mensaje, tamMaxPayload) : vector<string>{mensaje};

            int totalPkg = piezas.size();
            int seq = 1;

            for (auto &trozo : piezas) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                int totalSize = 1 + 5 + (int)trozo.length() + 5 + (int)nick.length();

                int offset = 4;
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
                seq++;
            }
        } else {
            enviarM(sock, "El usuario '" + destino + "' no esta conectado.");
        }
    }
    else if (tipo == 'B') {
        string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
             mensaje += pkgs[i].substr(10);
        }

        int sizeFijo = 1 + 5 + 5 + nick.length();
        int tamMaxPayload = 500 - (10 + sizeFijo);

        vector<string> piezas = (mensaje.length() > tamMaxPayload) ? 
            partir(mensaje, tamMaxPayload) : vector<string>{mensaje};

        int totalPkg = piezas.size();
        int seq = 1;

        for (auto &trozo : piezas) {
            memset(buffer, '#', 500);
            sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
            int totalSize = 1 + 5 + (int)trozo.length() + 5 + (int)nick.length();

            int offset = 4;
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

            lock_guard<mutex> lock(mapaMutex);
            for (auto &par : mapaSockets) {
                if (par.first != nick) {
                    write(par.second, buffer, 500);
                }
            }
            seq++;
        }
    }
    else if (tipo == 'F') {
        int tamDest = stoi(pkgs[0].substr(10, 5));
        string destino = pkgs[0].substr(15, tamDest);
        int tamFileName = stoi(pkgs[0].substr(15 + tamDest, 100));
        string fileName = pkgs[0].substr(15 + tamDest + 100, tamFileName);
        
        long tamFileTotalEnHeader = stol(pkgs[0].substr(15 + tamDest + 100 + tamFileName, 18));
        
        size_t posDatosPrimerPaquete = 15 + tamDest + 100 + tamFileName + 18;
        long lenPrimerTrozoActual = stol(pkgs[0].substr(posDatosPrimerPaquete - 18, 18));
        string hash = pkgs[0].substr(posDatosPrimerPaquete + lenPrimerTrozoActual, 5);

        string archivo;
        
        for (size_t i = 0; i < pkgs.size(); ++i) {
            if (i == 0) {
                archivo += pkgs[i].substr(posDatosPrimerPaquete, lenPrimerTrozoActual);
            } else {
                size_t posLenTrozoSubPaquete = 10;
                long lenActualTrozo = stol(pkgs[i].substr(posLenTrozoSubPaquete, 18));
                size_t posDatosSubPaquete = posLenTrozoSubPaquete + 18;
                archivo += pkgs[i].substr(posDatosSubPaquete, lenActualTrozo);
            }
        }
        
        lock_guard<mutex> lock(mapaMutex);
        if (mapaSockets.find(destino) != mapaSockets.end()) {
            int headerPrimerFragmentoReenvio = 1 + 5 + nick.length() + 100 + fileName.length() + 18 + 5;
            int tamMaxPrimerFragmentoReenvio = 500 - (10 + headerPrimerFragmentoReenvio);

            int headerSubFragmentoReenvio = 1 + 18 + 5;
            int tamMaxSubFragmentoReenvio = 500 - (10 + headerSubFragmentoReenvio);

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
            int seq = 1;

            for (auto &trozo : piezas_archivo_para_reenvio) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%02d%02d", (seq == totalPkg ? 0 : seq), totalPkg);
                
                int totalSize;
                int offset = 4;
                
                if (seq == 1) {
                    totalSize = 1 + 5 + (int)nick.length() + 100 + (int)fileName.length() +
                                18 + (int)trozo.length() + 5;
                    sprintf(buffer + offset, "%05d", totalSize);
                    offset += 5;
                    buffer[offset++] = 'f';
                    
                    sprintf(buffer + offset, "%05d", (int)nick.length());
                    offset += 5;
                    memcpy(buffer + offset, nick.c_str(), nick.length());
                    offset += nick.length();

                    sprintf(buffer + offset, "%0100d", (int)fileName.length());
                    offset += 100;
                    memcpy(buffer + offset, fileName.c_str(), fileName.length());
                    offset += fileName.length();
                    
                    sprintf(buffer + offset, "%018ld", trozo.length());
                    offset += 18;
                    memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();

                    memcpy(buffer + offset, hash.c_str(), 5);
                } else {
                    totalSize = 1 + 18 + (int)trozo.length() + 5;
                    sprintf(buffer + offset, "%05d", totalSize);
                    offset += 5;
                    buffer[offset++] = 'f';

                    sprintf(buffer + offset, "%018ld", trozo.length());
                    offset += 18;
                    memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();

                    memcpy(buffer + offset, hash.c_str(), 5);
                }
                write(mapaSockets[destino], buffer, 500);
                seq++;
            }
        } else {
            enviarM(sock, "Error: Destino no encontrado para enviar archivo.");
        }
    }
    else if (tipo == 'Q') {
        printf("\nEl cliente %s ha salido del chat\n", nick.c_str());
        lock_guard<mutex> lock(mapaMutex);
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
            memcpy(pkt, "0001", 4);
            memcpy(pkt + 4, "00010", 5);
            pkt[9] = 'X';
            memcpy(pkt + 10, partida.tablero, 9);
            write(sock, pkt, 500);
        } else {
            enviarM(sock, "La partida no esta activa para ver.");
        }
    }
    else if (tipo == 'P') {
        char pos = pkgs[0][10];
        char simb = pkgs[0][11];

        lock_guard<mutex> lock(partidaMutex);
        if (!((nick == partida.jugadorO && simb == 'O' && partida.turno == 'O') ||
              (nick == partida.jugadorX && simb == 'X' && partida.turno == 'X'))) {
            char errPkt[500];
            memset(errPkt, '#', 500);
            memcpy(errPkt, "0001", 4);
            memcpy(errPkt + 4, "00021", 5);
            int off = 9;
            errPkt[off++] = 'E';
            errPkt[off++] = '7';
            memcpy(errPkt + off, "00014", 5);
            off += 5;
            memcpy(errPkt + off, "No es tu turno", 14);
            write(sock, errPkt, 500);
            return;
        }

        int idx = pos - '1';
        if (idx < 0 || idx > 8 || partida.tablero[idx] != '_') {
            char errPkt[500];
            memset(errPkt, '#', 500);
            memcpy(errPkt, "0001", 4);
            memcpy(errPkt + 4, "00024", 5);
            int off = 9;
            errPkt[off++] = 'E';
            errPkt[off++] = '6';
            memcpy(errPkt + off, "00016", 5);
            off += 5;
            memcpy(errPkt + off, "Posicion ocupada", 16);
            write(sock, errPkt, 500);
            return;
        }

        partida.tablero[idx] = simb;

        if (ganador(simb)) {
            enviarX_aTodos();
            sleep(1);
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
    memcpy(buffer, "0001", 4);
    sprintf(buffer + 4, "%05d", tamTot);
    int off = 9;
    buffer[off++] = 'm';
    sprintf(buffer + off, "%05d", lenMsg);
    off += 5;
    memcpy(buffer + off, msg.c_str(), lenMsg);
    off += lenMsg;
    sprintf(buffer + off, "%05d", lenRem);
    off += 5;
    memcpy(buffer + off, remitente.c_str(), lenRem);
    write(sock, buffer, 500);
}

void enviarX_aTodos() {
    char pkt[500];
    memset(pkt, '#', 500);
    memcpy(pkt, "0001", 4);
    memcpy(pkt + 4, "00010", 5);
    pkt[9] = 'X';
    memcpy(pkt + 10, partida.tablero, 9);

    lock_guard<mutex> lock(mapaMutex);
    if (!partida.jugadorO.empty() && mapaSockets.count(partida.jugadorO))
        write(mapaSockets[partida.jugadorO], pkt, 500);
    if (!partida.jugadorX.empty() && mapaSockets.count(partida.jugadorX))
        write(mapaSockets[partida.jugadorX], pkt, 500);
    for (auto &esp : partida.espectadores)
        if (mapaSockets.count(esp))
            write(mapaSockets[esp], pkt, 500);
}

void enviarT(const string &nick, char simbolo) {
    char pkt[500];
    memset(pkt, '#', 500);
    memcpy(pkt, "0001", 4);
    memcpy(pkt + 4, "00002", 5);
    pkt[9] = 'T';
    pkt[10] = simbolo;
    lock_guard<mutex> lock(mapaMutex);
    if (mapaSockets.count(nick))
        write(mapaSockets[nick], pkt, 500);
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
    memcpy(pktO, "0001", 4);
    memcpy(pktO + 4, "00002", 5);
    pktO[9] = 'O';
    pktO[10] = (resultado == 'O') ? 'W' : (resultado == 'X') ? 'L' : 'E';
    
    memset(pktX, '#', 500);
    memcpy(pktX, "0001", 4);
    memcpy(pktX + 4, "00002", 5);
    pktX[9] = 'O';
    pktX[10] = (resultado == 'X') ? 'W' : (resultado == 'O') ? 'L' : 'E';
    
    memset(pktE, '#', 500);
    memcpy(pktE, "0001", 4);
    memcpy(pktE + 4, "00002", 5);
    pktE[9] = 'O';
    pktE[10] = 'E';

    lock_guard<mutex> lock1(mapaMutex);
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

        if (pkg.length() < 4) {
            cerr << "Error: Paquete demasiado corto para cabecera de fragmentacion." << endl;
            continue;
        }

        int seq = stoi(pkg.substr(0, 2));
        int tot = stoi(pkg.substr(2, 2));

        if (seq < 0 || seq > tot || tot <= 0) {
            cerr << "Error de fragmento: Valores de secuencia/total invalidos (seq=" << seq << ", tot=" << tot << ")." << endl;
            vc.clear();
            expected_total_pkgs = 0;
            continue;
        }

        int idx = (seq == 0) ? tot - 1 : seq - 1;

        if (expected_total_pkgs == 0) {
            expected_total_pkgs = tot;
            vc.resize(tot);
        } else if (tot != expected_total_pkgs) {
            cerr << "Error de fragmento: El numero total de paquetes cambio (esperado "
                 << expected_total_pkgs << ", recibido " << tot << "). Reiniciando reensamblaje." << endl;
            vc.clear();
            expected_total_pkgs = tot;
            vc.resize(tot);
        }

        if (idx < 0 || idx >= vc.size()) {
            cerr << "Error de fragmento: idx fuera de rango o invalido (idx=" << idx << ", vc.size=" << vc.size() << ")." << endl;
            continue;
        }

        if (!vc[idx].empty()) {
            cerr << "Advertencia: Fragmento " << idx + 1 << " ya recibido. Ignorando duplicado o error." << endl;
            continue;
        }

        vc[idx] = move(pkg);

        bool completo = true;
        for (int i = 0; i < expected_total_pkgs; ++i) {
            if (vc[i].empty()) {
                completo = false;
                break;
            }
        }

        if (completo) {
            char tipo = vc[0][9];
            vector<string> paquetesCompletos = move(vc);
            vc.clear();
            expected_total_pkgs = 0;

            procesarMensaje(sock, nick, paquetesCompletos, tipo);
        }
    }

    {
        lock_guard<mutex> lock1(mapaMutex);
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
        if (pkg.length() >= 10 && pkg.substr(0,4) == "0001" && pkg[9] == 'N') {
            int tamNick = stoi(pkg.substr(4, 5)) - 1;
            if (tamNick > 0 && (size_t)(10 + tamNick) <= pkg.length()) {
                string nick = pkg.substr(10, tamNick);
                
                lock_guard<mutex> lock(mapaMutex);
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
                cerr << "Error: Paquete de registro N malformado." << endl;
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