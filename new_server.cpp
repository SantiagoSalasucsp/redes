#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <map>
#include <vector>
#include <mutex>
#include <set>

using namespace std;

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 512;

int tablero[9] = { 0 };
int turno = 0;
int jugadas = 0;

mutex mtx;

// Estructuras para manejo UDP y fragmentación
map<string, map<int, map<int, string>>> receivedFragmentsUDP; // clienteId -> msgId -> numPaquete -> fragmento
map<string, map<int, int>> totalFragmentsExpectedUDP; // clienteId -> msgId -> total paquetes

// Mapeo clientes
map<string, sockaddr_in> clientesUDP;   // nombre -> dirección UDP
map<string, string> clientesNombres;   // clienteId (ip:puerto) -> nombre

pair<string, string> players; // jugadores
set<string> espectadores; // espectadores

// Helpers

string addrToString(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
    int port = ntohs(addr.sin_port);
    return string(ip) + ":" + to_string(port);
}

string to5Digits(int num) {
    string s = to_string(num);
    while (s.length() < 5) s = "0" + s;
    return s;
}

string ceros(int tam, string num) {
    string ret;
    int cifras = to_string(num.length()).length();
    if (cifras < tam) {
        ret.append(tam - cifras, '0');
    }
    return ret + to_string(num.length());
}

string ceros_int(int tam, int num) {
    string s = to_string(num);
    while ((int)s.length() < tam) {
        s = "0" + s;
    }
    return s;
}

string tablero_string() {
    string ret;
    for (int i = 0; i < 9; i++) {
        ret += to_string(tablero[i]);
    }
    return ret;
}

void resetGame() {
    for (int i = 0; i < 9; i++) tablero[i] = 0;
    turno = 0;
    jugadas = 0;
    players = make_pair("", "");
}

int victoria(int jugador) {
    if ((tablero[0] == jugador && tablero[1] == jugador && tablero[2] == jugador) ||
        (tablero[3] == jugador && tablero[4] == jugador && tablero[5] == jugador) ||
        (tablero[6] == jugador && tablero[7] == jugador && tablero[8] == jugador) ||
        (tablero[0] == jugador && tablero[3] == jugador && tablero[6] == jugador) ||
        (tablero[1] == jugador && tablero[4] == jugador && tablero[7] == jugador) ||
        (tablero[2] == jugador && tablero[5] == jugador && tablero[8] == jugador) ||
        (tablero[0] == jugador && tablero[4] == jugador && tablero[8] == jugador) ||
        (tablero[2] == jugador && tablero[4] == jugador && tablero[6] == jugador)) {
        return jugador;
    }
    return 0;
}

// Función para enviar paquetes UDP fragmentados
void sendPaquete(int sock, const sockaddr_in& clientAddr, const string& msg) {
    int tam = msg.size();
    int fragmentSize = 485;
    int totalPaquetes = (tam + fragmentSize - 1) / fragmentSize;

    for (int i = 1; i <= totalPaquetes; i++) {
        int inicio = (i - 1) * fragmentSize;
        int len = min(fragmentSize, tam - inicio);
        string fragmento = msg.substr(inicio, len);
        int numPaquete = (i == totalPaquetes) ? 0 : i;
        string paquete = to5Digits(numPaquete) + to5Digits(totalPaquetes) + to5Digits(tam) + fragmento;
        while (paquete.size() < 500) paquete += '#';
        sendto(sock, paquete.c_str(), paquete.size(), 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
    }
}

// Procesa fragmentos recibidos y reconstruye mensaje completo si ya llegó todo
bool procesarFragmentoServidor(const string& paquete, const string& clienteId, int& msgIdOut, string& mensajeCompleto) {
    string numPStr = paquete.substr(0, 5);
    string totalPStr = paquete.substr(5, 5);
    string tamMStr = paquete.substr(10, 5);
    string fragmento = paquete.substr(15);
    fragmento.erase(fragmento.find_last_not_of('#') + 1);

    int numP = stoi(numPStr);
    int totalP = stoi(totalPStr);
    int tamM = stoi(tamMStr);

    static map<string, int> currentMsgIdMap; // clienteId -> msgId

    //cout << "Fragmento recibido: numP=" << numP << ", totalP=" << totalP << ", clienteId=" << clienteId << endl;

    if (numP == 1) {
        currentMsgIdMap[clienteId]++;
        msgIdOut = currentMsgIdMap[clienteId];
    }
    else if (numP == 0) {
        if (currentMsgIdMap.find(clienteId) == currentMsgIdMap.end() || currentMsgIdMap[clienteId] == 0) {
            currentMsgIdMap[clienteId] = 1;
        }
        msgIdOut = currentMsgIdMap[clienteId];
    }
    else {
        if (currentMsgIdMap.find(clienteId) == currentMsgIdMap.end()) {
            currentMsgIdMap[clienteId] = 1;
        }
        msgIdOut = currentMsgIdMap[clienteId];
    }

    {
        lock_guard<mutex> lock(mtx);
        receivedFragmentsUDP[clienteId][msgIdOut][numP] = fragmento;
        totalFragmentsExpectedUDP[clienteId][msgIdOut] = totalP;
    }

    {
        lock_guard<mutex> lock(mtx);
        cout << "Fragmentos recibidos hasta ahora para msgId=" << msgIdOut << ": "
            << receivedFragmentsUDP[clienteId][msgIdOut].size() << "/" << totalP << endl;
        if ((int)receivedFragmentsUDP[clienteId][msgIdOut].size() == totalP) {
            string resultado;
            if (totalP == 1) {
                // Solo fragmento 0
                if (receivedFragmentsUDP[clienteId][msgIdOut].find(0) == receivedFragmentsUDP[clienteId][msgIdOut].end()) {
                    cout << "Falta fragmento 0 para msgId " << msgIdOut << endl;
                    return false;
                }
                resultado = receivedFragmentsUDP[clienteId][msgIdOut][0];
            }
            else {
                // Deben estar los fragmentos 1 a totalP-1 y también el 0
                for (int i = 1; i < totalP; i++) {
                    if (receivedFragmentsUDP[clienteId][msgIdOut].find(i) == receivedFragmentsUDP[clienteId][msgIdOut].end()) {
                        cout << "Falta fragmento " << i << " para msgId " << msgIdOut << endl;
                        return false;
                    }
                    resultado += receivedFragmentsUDP[clienteId][msgIdOut][i];
                }
                if (receivedFragmentsUDP[clienteId][msgIdOut].find(0) == receivedFragmentsUDP[clienteId][msgIdOut].end()) {
                    cout << "Falta fragmento 0 para msgId " << msgIdOut << endl;
                    return false;
                }
                resultado += receivedFragmentsUDP[clienteId][msgIdOut][0];
            }
            receivedFragmentsUDP[clienteId].erase(msgIdOut);
            totalFragmentsExpectedUDP[clienteId].erase(msgIdOut);
            mensajeCompleto = resultado;
            cout << "Mensaje completo reconstruido de " << clienteId << ": "
                << mensajeCompleto.substr(0, min((int)mensajeCompleto.size(), 50)) << "...\n";
            return true;
        }
    }
    return false;
}

// Función para obtener el nombre del cliente por su dirección
string obtenerNombreCliente(const sockaddr_in& clientAddr) {
    string clienteId = addrToString(clientAddr);
    if (clientesNombres.find(clienteId) != clientesNombres.end()) {
        return clientesNombres[clienteId];
    }
    return "Desconocido";
}

// Procesa comandos de cliente
void procesarComando(int sock, const sockaddr_in& clientAddr, const string& clienteId, const string& clientName, const string& mensaje) {
    if (mensaje.empty()) return;

    char cmd = mensaje[0];
    string contenido = mensaje.substr(1);

    switch (cmd) {
    case 't': {
        // Login (no debería llegar aquí, ya procesado)
        break;
    }
    case 'j': {
        if (players.first.empty()) {
            players.first = clientName;
            sendPaquete(sock, clientAddr, "TX");
        }
        else if (players.second.empty() && players.first != clientName) {
            players.second = clientName;
            sendPaquete(sock, clientAddr, "TO");
            // Enviar tablero solo a jugadores y espectadores
            string boardMsg = "X" + tablero_string();

            cout << "Enviando tablero a jugadores y espectadores tras inicio partida.\n";

            if (!players.first.empty() && clientesUDP.find(players.first) != clientesUDP.end()) {
                sendPaquete(sock, clientesUDP[players.first], boardMsg);
            }
            if (!players.second.empty() && clientesUDP.find(players.second) != clientesUDP.end()) {
                sendPaquete(sock, clientesUDP[players.second], boardMsg);
            }

            for (const auto& espectador : espectadores) {
                if (clientesUDP.find(espectador) != clientesUDP.end()) {
                    sendPaquete(sock, clientesUDP[espectador], boardMsg);
                }
            }
        }
        else {
            string error = "E" + ceros(2, "Partida llena") + "Partida llena";
            sendPaquete(sock, clientAddr, error);
        }
        break;
    }
    case 'v': {
        // Agregar cliente a lista de espectadores
        if (espectadores.find(clientName) == espectadores.end()) {
            espectadores.insert(clientName);
            cout << "Cliente " << clientName << " agregado como espectador.\n";
        }
        sendPaquete(sock, clientAddr, "X" + tablero_string());
        break;
    }
    case 'p': {
        if (clientName != players.first && clientName != players.second) {
            string error = "E" + ceros(2, "No está jugando") + "No está jugando";
            sendPaquete(sock, clientAddr, error);
            break;
        }
        if (contenido.size() < 1) break;
        int pos = stoi(contenido.substr(0, 1));
        if ((clientName == players.first && turno == 0) ||
            (clientName == players.second && turno == 1)) {
            if (tablero[pos] == 0) {
                tablero[pos] = (turno == 0) ? 1 : 2;
                jugadas++;
                int winner = victoria(turno == 0 ? 1 : 2);
                if (winner) {
                    string winnerName = (winner == 1) ? players.first : players.second;
                    string winnerMsg = "O" + ceros_int(2, (int)winnerName.size()) + winnerName;

                    cout << "Jugador " << winnerName << " ha ganado la partida.\n";

                    // Enviar mensaje ganador solo a jugadores y espectadores
                    if (!players.first.empty() && clientesUDP.find(players.first) != clientesUDP.end()) {
                        sendPaquete(sock, clientesUDP[players.first], winnerMsg);
                    }
                    if (!players.second.empty() && clientesUDP.find(players.second) != clientesUDP.end()) {
                        sendPaquete(sock, clientesUDP[players.second], winnerMsg);
                    }
                    for (const auto& espectador : espectadores) {
                        if (clientesUDP.find(espectador) != clientesUDP.end()) {
                            sendPaquete(sock, clientesUDP[espectador], winnerMsg);
                        }
                    }

                    resetGame();
                }
                else if (jugadas == 9) {
                    string empateStr = "Empate";
                    string drawMsg = "O" + ceros_int(2, (int)empateStr.size()) + empateStr;

                    cout << "La partida terminó en empate.\n";

                    // Enviar mensaje empate solo a jugadores y espectadores
                    if (!players.first.empty() && clientesUDP.find(players.first) != clientesUDP.end()) {
                        sendPaquete(sock, clientesUDP[players.first], drawMsg);
                    }
                    if (!players.second.empty() && clientesUDP.find(players.second) != clientesUDP.end()) {
                        sendPaquete(sock, clientesUDP[players.second], drawMsg);
                    }
                    for (const auto& espectador : espectadores) {
                        if (clientesUDP.find(espectador) != clientesUDP.end()) {
                            sendPaquete(sock, clientesUDP[espectador], drawMsg);
                        }
                    }

                    resetGame();
                }
                else {
                    turno = (turno + 1) % 2;
                    string boardMsg = "X" + tablero_string();

                    // Enviar tablero solo a jugadores y espectadores
                    if (!players.first.empty() && clientesUDP.find(players.first) != clientesUDP.end()) {
                        sendPaquete(sock, clientesUDP[players.first], boardMsg);
                    }
                    if (!players.second.empty() && clientesUDP.find(players.second) != clientesUDP.end()) {
                        sendPaquete(sock, clientesUDP[players.second], boardMsg);
                    }
                    for (const auto& espectador : espectadores) {
                        if (clientesUDP.find(espectador) != clientesUDP.end()) {
                            sendPaquete(sock, clientesUDP[espectador], boardMsg);
                        }
                    }
                }
            }
            else {
                string error = "E" + ceros(2, "Posicion ocupada") + "Posicion ocupada";
                sendPaquete(sock, clientAddr, error);
            }
        }
        else {
            string error = "E" + ceros(2, "No es tu turno") + "No es tu turno";
            sendPaquete(sock, clientAddr, error);
        }
        break;
    }
    case 'l': {
        int totalUsers = (int)clientesUDP.size();
        string nombres;
        for (auto& kv : clientesUDP) {
            nombres += kv.first + ",";
        }
        if (!nombres.empty()) nombres.pop_back();
        string msg = "L" + ceros_int(2, totalUsers) + ceros_int(3, (int)nombres.length()) + nombres;

        cout << "Enviando lista de usuarios: " << msg << endl;

        sendPaquete(sock, clientAddr, msg);
        break;
    }
    case 'b': {
        if (contenido.size() < 2) break;
        int msgSize = stoi(contenido.substr(0, 2));
        string msg = contenido.substr(2);
        string broadcast = "B" + ceros(2, clientName) + clientName + ceros(2, msg) + msg;
        for (auto& kv : clientesUDP) {
            sendPaquete(sock, kv.second, broadcast);
        }
        break;
    }
    case 'm': {
        if (contenido.size() < 4) break;
        int destSize = stoi(contenido.substr(0, 2));
        string destinatario = contenido.substr(2, destSize);
        int msgSize = stoi(contenido.substr(2 + destSize, 2));
        string msg = contenido.substr(4 + destSize, msgSize);

        if (clientesUDP.find(destinatario) != clientesUDP.end()) {
            string privMsg = "M" + ceros(2, clientName) + clientName + ceros(2, msg) + msg;
            sendPaquete(sock, clientesUDP[destinatario], privMsg);
        }
        else {
            string error = "E" + ceros(2, "Usuario no encontrado") + "Usuario no encontrado";
            sendPaquete(sock, clientAddr, error);
        }
        break;
    }
    case 'F': {
        // Manejo de archivos - adaptado para el cliente proporcionado
        cout << "Procesando archivo recibido de " << clientName << endl;
        
        int idx = 0;
        
        // Leer tamaño del destinatario (5 dígitos)
        if (contenido.size() < 5) break;
        int destSize = stoi(contenido.substr(idx, 5));
        idx += 5;
        
        // Leer destinatario
        if (contenido.size() < idx + destSize) break;
        string destinatario = contenido.substr(idx, destSize);
        idx += destSize;
        
        // Leer tamaño del nombre del archivo (100 dígitos)
        if (contenido.size() < idx + 100) break;
        int nameSize = stoi(contenido.substr(idx, 100));
        idx += 100;
        
        // Leer nombre del archivo
        if (contenido.size() < idx + nameSize) break;
        string fileName = contenido.substr(idx, nameSize);
        idx += nameSize;
        
        // Leer tamaño del archivo (18 dígitos)
        if (contenido.size() < idx + 18) break;
        int fileSize = stoi(contenido.substr(idx, 18));
        idx += 18;
        
        // Leer contenido del archivo
        if (contenido.size() < idx + fileSize) break;
        string fileContent = contenido.substr(idx, fileSize);
        idx += fileSize;
        
        // Leer hash (5 dígitos)
        if (contenido.size() < idx + 5) break;
        string hash = contenido.substr(idx, 5);

        cout << "Archivo: " << fileName << ", Destino: " << destinatario 
             << ", Tamaño: " << fileSize << ", Hash: " << hash << endl;

        // Verificar si el destinatario existe
        if (clientesUDP.find(destinatario) != clientesUDP.end()) {
            // Reenviar archivo al destinatario con el formato correcto
            string msg = "F" + ceros_int(5, (int)clientName.size()) + clientName + 
                        ceros_int(100, (int)fileName.size()) + fileName + 
                        ceros_int(18, fileSize) + fileContent + hash;
            
            sendPaquete(sock, clientesUDP[destinatario], msg);
            cout << "Archivo reenviado de " << clientName << " a " << destinatario << endl;
        }
        else {
            string error = "E" + ceros(2, "Usuario no encontrado") + "Usuario no encontrado";
            sendPaquete(sock, clientAddr, error);
            cout << "Usuario destinatario " << destinatario << " no encontrado" << endl;
        }
        break;
    }
    case 'q': {
        cout << "Cliente " << clientName << " desconectado\n";
        clientesUDP.erase(clientName);
        clientesNombres.erase(clienteId);
        espectadores.erase(clientName);
        if (players.first == clientName) players.first = "";
        if (players.second == clientName) players.second = "";
        break;
    }
    }
}

int main() {
    int sock;
    sockaddr_in serverAddr;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        cerr << "Error creando socket\n";
        return 1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(PORT);

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error al bindear\n";
        return 1;
    }

    clientesUDP.clear();
    clientesNombres.clear();
    cout << "Servidor iniciado en puerto " << PORT << endl;
    cout << "Esperando conexiones..." << endl;

    char buffer[BUFFER_SIZE];
    while (true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);

        int bytesReceived = recvfrom(sock, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &addrLen);
        if (bytesReceived <= 0) {
            cerr << "Error recibiendo datos\n";
            continue;
        }
        string paquete(buffer, bytesReceived);
        string clienteId = addrToString(clientAddr);

        //cout << "Paquete recibido de " << clienteId << ": " << paquete.substr(0, min(50, (int)paquete.size())) << "..." << endl;

        int msgId;
        string mensajeCompleto;
        if (!procesarFragmentoServidor(paquete, clienteId, msgId, mensajeCompleto)) {
            continue;
        }

        cout << "Mensaje completo de " << clienteId << ": " << mensajeCompleto.substr(0, min(50, (int)mensajeCompleto.size())) << "..." << endl;

        // Si es login ('t'), procesar aquí mismo
        if (!mensajeCompleto.empty() && mensajeCompleto[0] == 't') {
            if (mensajeCompleto.size() < 3) continue;
            int nameLen = stoi(mensajeCompleto.substr(1, 2));
            if (mensajeCompleto.size() < 3 + nameLen) continue;
            string nombre = mensajeCompleto.substr(3, nameLen);

            cout << "Intento de login: " << nombre << " desde " << clienteId << endl;

            bool nameExists = false;
            for (auto& kv : clientesUDP) {
                if (kv.first == nombre) {
                    nameExists = true;
                    break;
                }
            }

            if (nameExists) {
                cout << "Nombre ya en uso: " << nombre << endl;
                string resp = "N";
                sendto(sock, resp.c_str(), resp.size(), 0, (sockaddr*)&clientAddr, addrLen);
            }
            else {
                clientesUDP[nombre] = clientAddr;
                clientesNombres[clienteId] = nombre;

                cout << "Cliente conectado: " << nombre << " desde " << clienteId << endl;
                cout << "Clientes actuales: ";
                for (auto& kv : clientesUDP) {
                    cout << kv.first << " ";
                }
                cout << endl;
                
                string resp = "O";
                sendto(sock, resp.c_str(), resp.size(), 0, (sockaddr*)&clientAddr, addrLen);
            }
            continue;
        }

        // Verificar que el cliente esté autenticado
        if (clientesNombres.find(clienteId) == clientesNombres.end()) {
            string resp = "E" + ceros(2, "Debe iniciar sesión") + "Debe iniciar sesión";
            sendPaquete(sock, clientAddr, resp);
            continue;
        }
        
        string clientName = clientesNombres[clienteId];
        procesarComando(sock, clientAddr, clienteId, clientName, mensajeCompleto);
    }

    close(sock);
    return 0;
}
