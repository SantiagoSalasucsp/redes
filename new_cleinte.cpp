#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <atomic>
#include <fstream>
#include <vector>
#include <mutex>
#include <map>
#include <cmath>
#include <limits>

using namespace std;

constexpr int PORT = 8080;
constexpr int BUFFER_SIZE = 512;
constexpr char SERVER_ADDRESS[] = "127.0.0.1";
atomic<bool> shouldRun(true);

mutex mtx;
map<int, map<int, string>> receivedFragments;
map<int, int> totalFragmentsExpected;

int calcularHash(const char* data, size_t length) {
    int hash = 0;
    for (size_t i = 0; i < length; ++i) {
        hash = (hash + (unsigned char)data[i]) % 100000;
    }
    return hash;
}

string to5Digits(int num) {
    string s = to_string(num);
    while (s.length() < 5) s = "0" + s;
    return s;
}

string crearPaquete(int numPaquete, int totalPaquetes, int tamMensaje, const string& fragmento) {
    string numP = (numPaquete == 0) ? "00000" : to5Digits(numPaquete);
    string totalP = to5Digits(totalPaquetes);
    string tamM = to5Digits(tamMensaje);
    string paquete = numP + totalP + tamM + fragmento;
    while (paquete.size() < 500) paquete += '#';
    return paquete;
}

void enviarMensaje(int sock, const sockaddr_in& addr, const string& mensaje) {
    int tam = mensaje.size();
    int fragmentSize = 485;
    int totalPaquetes = (tam + fragmentSize - 1) / fragmentSize;

    for (int i = 1; i <= totalPaquetes; i++) {
        int inicio = (i - 1) * fragmentSize;
        int len = min(fragmentSize, tam - inicio);
        string fragmento = mensaje.substr(inicio, len);
        int numPaquete = (i == totalPaquetes) ? 0 : i;
        string paquete = crearPaquete(numPaquete, totalPaquetes, tam, fragmento);
        sendto(sock, paquete.c_str(), paquete.size(), 0, (sockaddr*)&addr, sizeof(addr));
    }
}

bool procesarFragmento(const string& paquete, int& msgIdOut, string& mensajeCompleto) {
    string numPStr = paquete.substr(0, 5);
    string totalPStr = paquete.substr(5, 5);
    string tamMStr = paquete.substr(10, 5);
    string fragmento = paquete.substr(15, 485);
    fragmento.erase(fragmento.find_last_not_of('#') + 1);

    int numP = stoi(numPStr);
    int totalP = stoi(totalPStr);
    int tamM = stoi(tamMStr);

    static int currentMsgId = 0;

    if (numP == 1) {
        currentMsgId++;
        msgIdOut = currentMsgId;
        lock_guard<mutex> lock(mtx);
        receivedFragments[msgIdOut][numP] = fragmento;
        totalFragmentsExpected[msgIdOut] = totalP;
    }
    else if (numP == 0) {
        msgIdOut = currentMsgId;
        lock_guard<mutex> lock(mtx);
        receivedFragments[msgIdOut][numP] = fragmento;
        totalFragmentsExpected[msgIdOut] = totalP;
    }
    else {
        msgIdOut = currentMsgId;
        lock_guard<mutex> lock(mtx);
        receivedFragments[msgIdOut][numP] = fragmento;
    }

    lock_guard<mutex> lock(mtx);
    if ((int)receivedFragments[msgIdOut].size() == totalFragmentsExpected[msgIdOut]) {
        string resultado;
        if (totalP == 1) {
            if (receivedFragments[msgIdOut].find(0) != receivedFragments[msgIdOut].end()) {
                resultado = receivedFragments[msgIdOut][0];
            }
            else {
                return false;
            }
        }
        else {
            for (int i = 1; i <= totalP; i++) {
                if (receivedFragments[msgIdOut].find(i) == receivedFragments[msgIdOut].end()) {
                    return false;
                }
                resultado += receivedFragments[msgIdOut][i];
            }
        }
        receivedFragments.erase(msgIdOut);
        totalFragmentsExpected.erase(msgIdOut);
        mensajeCompleto = resultado;
        return true;
    }
    return false;
}

void printTablero(const string& tablero) {
    cout << "Tablero actual:\n";
    cout << "-------\n";
    for (int i = 0; i < 9; i++) {
        char c;
        if (tablero[i] == '0') c = ' ';
        else if (tablero[i] == '1') c = 'X';
        else if (tablero[i] == '2') c = 'O';
        else c = '?';
        cout << "|" << c;
        if ((i + 1) % 3 == 0) {
            cout << "|\n";
            cout << "-------\n";
        }
    }
}

void receiveMessages(int sock, sockaddr_in serverAddr) {
    char buffer[BUFFER_SIZE];
    socklen_t addrLen = sizeof(serverAddr);
    while (shouldRun.load()) {
        int bytesReceived = recvfrom(sock, buffer, BUFFER_SIZE, 0, (sockaddr*)&serverAddr, &addrLen);
        if (bytesReceived <= 0) {
            cerr << "Error al recibir mensaje del servidor\n";
            break;
        }
        string paquete(buffer, bytesReceived);

        int msgId;
        string mensajeCompleto;
        if (!procesarFragmento(paquete, msgId, mensajeCompleto)) {
            continue;
        }
        if (mensajeCompleto.empty()) continue;

        char tipo = mensajeCompleto[0];
        string contenido = mensajeCompleto.substr(1);

        switch (tipo) {
        case 'T': {
            if (contenido.size() < 1) break;
            cout << "Tu turno: Juegas como '" << contenido[0] << "'\n";
            break;
        }
        case 'X': {
            if (contenido.size() < 9) break;
            printTablero(contenido.substr(0, 9));
            break;
        }
        case 'O': {
            if (contenido.size() < 2) break;
            int tam = stoi(contenido.substr(0, 2));
            cout << "ˇGanador: " << contenido.substr(2, tam) << "!\n";
            break;
        }
        case 'E': {
            if (contenido.size() < 2) break;
            int tam = stoi(contenido.substr(0, 2));
            cerr << "Error: " << contenido.substr(2, tam) << "\n";
            break;
        }
        case 'L': {
            if (contenido.size() < 5) break;
            int total = stoi(contenido.substr(0, 2));
            int tam = stoi(contenido.substr(2, 3));
            cout << "Usuarios conectados (" << total << "): " << contenido.substr(5, tam) << "\n";
            break;
        }
        case 'B': {
            if (contenido.size() < 4) break;
            int tamRem = stoi(contenido.substr(0, 2));
            string remitente = contenido.substr(2, tamRem);
            int tamMsg = stoi(contenido.substr(2 + tamRem, 2));
            string msg = contenido.substr(4 + tamRem, tamMsg);
            cout << "Broadcast de " << remitente << ": " << msg << "\n";
            break;
        }
        case 'M': {
            if (contenido.size() < 4) break;
            int tamRem = stoi(contenido.substr(0, 2));
            string remitente = contenido.substr(2, tamRem);
            int tamMsg = stoi(contenido.substr(2 + tamRem, 2));
            string msg = contenido.substr(4 + tamRem, tamMsg);
            cout << "Mensaje de " << remitente << ": " << msg << "\n";
            break;
        }
        case 'F': {
            int idx = 0;
            int sizeSender = stoi(mensajeCompleto.substr(1, 5));
            idx += 5;
            string sender = mensajeCompleto.substr(1 + idx, sizeSender);
            idx += sizeSender;
            int sizeFileName = stoi(mensajeCompleto.substr(1 + idx, 100));
            idx += 100;
            string fileName = mensajeCompleto.substr(1 + idx, sizeFileName);
            idx += sizeFileName;
            int sizeFile = stoi(mensajeCompleto.substr(1 + idx, 18));
            idx += 18;
            string fileContent = mensajeCompleto.substr(1 + idx, sizeFile);
            idx += sizeFile;
            string hash = mensajeCompleto.substr(1 + idx, 5);

            int calculatedHash = 0;
            for (char c : fileContent) {
                calculatedHash = (calculatedHash + (unsigned char)c) % 100000;
            }
            if (to5Digits(calculatedHash) == hash) {
                size_t dotPos = fileName.find_last_of('.');
                string newFileName;
                if (dotPos != string::npos) {
                    newFileName = fileName.substr(0, dotPos) + "_recibido" + fileName.substr(dotPos);
                }
                else {
                    newFileName = fileName + "_recibido";
                }
                ofstream outFile(newFileName, ios::binary);
                outFile.write(fileContent.data(), fileContent.size());
                outFile.close();
                cout << "Archivo recibido: " << newFileName << " (" << sizeFile << " bytes, hash OK)" << endl;
            }
            else {
                cerr << "Error: Hash no coincide para el archivo " << fileName << endl;
            }
            break;
        }
        case 'N': {
            cerr << "Nombre no disponible, intente otro.\n";
            break;
        }
        default:
            break;
        }
    }
}

string readFile(const string& filePath) {
    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        cerr << "Error al abrir el archivo\n";
        return "";
    }
    file.seekg(0, ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, ios::beg);
    vector<char> buffer(fileSize);
    file.read(buffer.data(), fileSize);
    return string(buffer.data(), buffer.size());
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
    string ret;
    int cifras = (num == 0) ? 1 : (int)log10(num) + 1;
    if (cifras < tam) {
        ret.append(tam - cifras, '0');
    }
    return ret + to_string(num);
}

void enviarArchivoUDP(const string& destino, const string& rutaArchivo) {
    string nombreArchivo;
    size_t posBarra = rutaArchivo.find_last_of("/\\");
    if (posBarra != string::npos) {
        nombreArchivo = rutaArchivo.substr(posBarra + 1);
    } else {
        nombreArchivo = rutaArchivo;
    }

    ifstream archivoEntrada(rutaArchivo, ios::in | ios::binary);
    if (!archivoEntrada.is_open()) {
        cerr << "No se pudo abrir el archivo " << rutaArchivo << "\n";
        return;
    }
    vector<char> datosArchivo((istreambuf_iterator<char>(archivoEntrada)),
                 istreambuf_iterator<char>());
    long tamanoArchivo = datosArchivo.size();
    archivoEntrada.close();

    int valorHash = calcularHash(datosArchivo.data(), tamanoArchivo);
    string hashStr = to5Digits(valorHash);

    string campoTamanoNombre = ceros_int(100, nombreArchivo.size());

    string data = "F" + ceros(5, destino) + destino + campoTamanoNombre + nombreArchivo + 
                 ceros_int(18, tamanoArchivo) + string(datosArchivo.data(), datosArchivo.size()) + hashStr;

    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) {
        cerr << "Error creando socket UDP\n";
        return;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(45001);
    inet_pton(AF_INET, SERVER_ADDRESS, &serverAddr.sin_addr);

    enviarMensaje(udpSocket, serverAddr, data);
    cout << "Archivo enviado vía UDP.\n";
    close(udpSocket);
}

string processMessage(string message) {
    if (message == "l" || message == "j" || message == "v") {
        return message;
    }
    else if (message.size() >= 2 && message[0] == 'p' && message[1] == ' ') {
        return "p" + message.substr(2);
    }
    else if (message.size() >= 2 && message[0] == 'b' && message[1] == ' ') {
        string contenido = message.substr(2);
        string msg = "b" + ceros(2, contenido) + contenido;
        return msg;
    }
    else if (message.size() >= 2 && message[0] == 'm' && message[1] == ' ') {
        string contenido = message.substr(2);
        size_t spacePos = contenido.find(' ');
        if (spacePos == string::npos) {
            cerr << "Formato: m destinatario mensaje\n";
            return "";
        }
        string destinatario = contenido.substr(0, spacePos);
        string msg = contenido.substr(spacePos + 1);
        string mensaje = "m" + ceros(2, destinatario) + destinatario + ceros(2, msg) + msg;
        return mensaje;
    }
    else if (message.size() >= 2 && message[0] == 'f' && message[1] == ' ') {
        size_t firstSpace = message.find(' ', 2);
        if (firstSpace == string::npos) {
            cerr << "Formato: f destinatario ruta_archivo\n";
            return "";
        }
        string destinatario = message.substr(2, firstSpace - 2);
        string filePath = message.substr(firstSpace + 1);
        enviarArchivoUDP(destinatario, filePath);
        return "";
    }
    else if (message == "q") {
        return "q";
    }
    else {
        cerr << "Comando no reconocido\n";
        return "";
    }
}

void login(int sock, const sockaddr_in& serverAddr) {
    char buffer[BUFFER_SIZE];
    while (true) {
        cout << "Ingrese su nombre: ";
        string nombre;
        getline(cin, nombre);
        string mensaje = "t" + ceros(2, nombre) + nombre;
        enviarMensaje(sock, serverAddr, mensaje);

        sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        int bytesReceived = recvfrom(sock, buffer, BUFFER_SIZE, 0, (sockaddr*)&fromAddr, &fromLen);
        if (bytesReceived <= 0) {
            cerr << "Error en login\n";
            continue;
        }
        string respuesta(buffer, bytesReceived);
        if (respuesta[0] == 'O') {
            cout << "Bienvenido.\n";
            break;
        }
        else if (respuesta[0] == 'N') {
            cout << "Nombre ya en uso. Intente otro.\n";
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
    serverAddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_ADDRESS, &serverAddr.sin_addr) <= 0) {
        cerr << "Dirección no válida\n";
        return 1;
    }

    login(sock, serverAddr);

    thread receiver(receiveMessages, sock, serverAddr);

    while (true) {
        cout << "> ";
        string message;
        getline(cin, message);
        if (message == "q") {
            enviarMensaje(sock, serverAddr, "q");
            shouldRun = false;
            shutdown(sock, SHUT_RDWR);
            receiver.join();
            close(sock);
            return 0;
        }
        string msg = processMessage(message);
        if (msg.empty()) continue;

        if (msg[0] == 'f') {
            continue; // Ya se manejó en processMessage
        }
        else {
            string paquete = crearPaquete(0, 1, msg.size(), msg);
            sendto(sock, paquete.c_str(), paquete.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
        }
    }
}
