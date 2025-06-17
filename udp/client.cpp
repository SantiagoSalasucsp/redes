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
#include <algorithm> // Para std::min

std::string nickname;
std::atomic<bool> envLista(false);
std::atomic<bool> enPartida(false);
std::atomic<bool> esEspectador(false);
std::atomic<char> simbolo{'\0'};
std::atomic<bool> tuTurno(false);
std::atomic<bool> preguntaVer(false);
std::mutex coutMux;
int socketGlobal;
std::mutex fileMutex; // Mutex para proteger el acceso al archivo

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

void clearInputBuffer() {
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void enviarNickname(int sock) {
    char buffer[500];
    memset(buffer, '#', 500);
    int totalSize = 1 + nickname.length(); // 'N' + nickname length
    memcpy(buffer, "0000100001", 10); // 5 dígitos para seq y tot
    sprintf(buffer + 10, "%05d", totalSize); // Offset ajustado
    buffer[15] = 'N'; // Offset ajustado
    memcpy(buffer + 16, nickname.c_str(), nickname.length()); // Offset ajustado
    write(sock, buffer, 500);
}

void recibirMensajes(int sock) {
    char buffer[500];
    std::vector<std::string> paquetesRecibidos;
    int expected_total_pkgs = 0;
    std::string current_file_name;
    std::ofstream current_output_file;
    int current_hash_expected = 0;
    
    // Contadores para el progreso del archivo
    int total_file_packets = 0;
    int packets_received_count = 0;

    // Offset para el tipo de mensaje y datos cambia de 11 a 15 debido a 5 dígitos para seq y tot
    const int PROTOCOL_HEADER_LEN = 15; // 5 (seq) + 5 (tot) + 5 (len) + 1 (tipo)

    while (true) {
        int n = read(sock, buffer, 500);
        if (n <= 0) {
            if (n == 0) {
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "\nServidor desconectado." << std::endl;
            } else {
                perror("read error");
            }
            break;
        }

        std::string pkg(buffer, n);

        if (pkg.length() < PROTOCOL_HEADER_LEN) { // Mínimo PROTOCOL_HEADER_LEN
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Error: Paquete demasiado corto para cabecera de protocolo (n=" << n << ", esperado min " << PROTOCOL_HEADER_LEN << ")." << std::endl;
            continue;
        }

        int seq = std::stoi(pkg.substr(0, 5)); // Ahora 5 dígitos
        int tot = std::stoi(pkg.substr(5, 5)); // Ahora 5 dígitos
        char tipo = pkg[15]; // Offset ajustado

        // Manejo especial para mensajes de una sola parte que no necesitan reensamblaje
        // y para los cuales seq=0 y tot=1 significa el único y último paquete.
        if (seq == 0 && tot == 1) { 
            if (tipo == 'A') { // Mensaje de Acknowledgment de archivo
                std::lock_guard<std::mutex> lock(coutMux);
                // El resultado es el primer caracter después de PROTOCOL_HEADER_LEN
                // y el mensaje es el resto. El len que se envía en el ACK es el len de resultado + mensaje.
                char resultado = pkg[PROTOCOL_HEADER_LEN]; 
                std::string mensaje = pkg.substr(PROTOCOL_HEADER_LEN + 1);
                if (resultado == 'S') {
                    std::cout << "\n[ACK Archivo OK]: " << mensaje << std::endl;
                } else {
                    std::cerr << "\n[ACK Archivo FALLO]: " << mensaje << std::endl;
                }
                // Limpiar estado de fragmentación si existiera alguno
                paquetesRecibidos.clear();
                expected_total_pkgs = 0;
                current_file_name.clear();
                if (current_output_file.is_open()) { // Asegurarse de cerrar si estaba abierto por algún error
                    current_output_file.close();
                    std::cout << "DEBUG: Archivo cerrado tras recibir ACK." << std::endl;
                }
                continue; // Procesado, ir al siguiente paquete
            }
        }


        if (seq < 0 || seq > tot || tot <= 0) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Error de fragmento: Valores de secuencia/total invalidos (seq=" << seq << ", tot=" << tot << ")." << std::endl;
            paquetesRecibidos.clear();
            expected_total_pkgs = 0;
            if (current_output_file.is_open()) {
                current_output_file.close();
                std::cout << "DEBUG: Archivo cerrado debido a error de fragmento." << std::endl;
            }
            continue;
        }

        int idx = (seq == 0) ? tot - 1 : seq - 1; // seq=0 significa el último paquete, su índice es tot-1

        if (expected_total_pkgs == 0) {
            expected_total_pkgs = tot;
            paquetesRecibidos.resize(tot);
            if (tipo == 'f') { // Si es un archivo, inicializar contadores
                total_file_packets = tot;
                packets_received_count = 0;
            }
        } else if (tot != expected_total_pkgs) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Error de fragmento: El numero total de paquetes cambio (esperado "
                      << expected_total_pkgs << ", recibido " << tot << "). Reiniciando reensamblaje." << std::endl;
            paquetesRecibidos.clear();
            expected_total_pkgs = tot;
            paquetesRecibidos.resize(tot);
            if (current_output_file.is_open()) {
                current_output_file.close();
                std::cout << "DEBUG: Archivo cerrado debido a cambio en total de paquetes." << std::endl;
            }
            if (tipo == 'f') { // Si es un archivo, reinicializar contadores
                total_file_packets = tot;
                packets_received_count = 0;
            }
        }

        if (idx < 0 || idx >= paquetesRecibidos.size()) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Error de fragmento: idx fuera de rango o invalido (idx=" << idx << ", paquetesRecibidos.size=" << paquetesRecibidos.size() << ")." << std::endl;
            continue;
        }

        if (!paquetesRecibidos[idx].empty()) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Advertencia: Fragmento " << idx + 1 << " ya recibido. Ignorando duplicado o error." << std::endl;
            continue;
        }

        paquetesRecibidos[idx] = std::move(pkg);
        
        if (tipo == 'f') {
            packets_received_count++;
            std::lock_guard<std::mutex> lock(coutMux);
            std::cout << "Recibiendo archivo... Paquete " << packets_received_count << " de " << total_file_packets << std::endl;
        } else {
            // Mensaje de depuración solo para fragmentos que no son de archivo
            // std::lock_guard<std::mutex> lock(coutMux);
            // std::cout << "Recibiendo fragmento " << idx + 1 << " de " << expected_total_pkgs << std::endl;
        }

        bool completo = true;
        for (int i = 0; i < expected_total_pkgs; ++i) {
            if (paquetesRecibidos[i].empty()) {
                completo = false;
                break;
            }
        }

        if (completo) {
            // Usar unique_lock para coutMux para poder desbloquearlo explícitamente
            std::unique_lock<std::mutex> coutLock(coutMux); 
            if (tipo == 'l') {
                std::string listaUsuarios;
                for (const auto& p : paquetesRecibidos) {
                    listaUsuarios += p.substr(PROTOCOL_HEADER_LEN); // Offset ajustado
                }
                std::cout << "\nUsuarios conectados: " << listaUsuarios << std::endl;
                envLista = true;
            }
            else if (tipo == 'm') {
                std::string remitente;
                std::string mensaje;
                int tamMsg = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN, 5)); // Offset ajustado
                int tamRem = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5 + tamMsg, 5)); // Offset ajustado
                
                mensaje = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5, tamMsg); // Offset ajustado
                remitente = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5 + tamMsg + 5, tamRem); // Offset ajustado

                for (size_t i = 1; i < paquetesRecibidos.size(); ++i) {
                    mensaje += paquetesRecibidos[i].substr(PROTOCOL_HEADER_LEN); // Offset ajustado
                }
                std::cout << "\n[MENSAJE DE " << remitente << "]: " << mensaje << std::endl;
            }
            else if (tipo == 'b') {
                std::string remitente;
                std::string mensaje;
                int tamMsg = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN, 5)); // Offset ajustado
                int tamRem = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5 + tamMsg, 5)); // Offset ajustado
                
                mensaje = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5, tamMsg); // Offset ajustado
                remitente = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5 + tamMsg + 5, tamRem); // Offset ajustado

                for (size_t i = 1; i < paquetesRecibidos.size(); ++i) {
                    mensaje += paquetesRecibidos[i].substr(PROTOCOL_HEADER_LEN); // Offset ajustado
                }
                std::cout << "\n[BROADCAST DE " << remitente << "]: " << mensaje << std::endl;
            }
            else if (tipo == 'X') {
                char tablero[9];
                memcpy(tablero, paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN, 9).c_str(), 9); // Offset ajustado
                std::cout << "\n--- Tablero Actualizado ---" << std::endl;
                std::cout << tablero[0] << "|" << tablero[1] << "|" << tablero[2] << std::endl;
                std::cout << "-+-+-" << std::endl;
                std::cout << tablero[3] << "|" << tablero[4] << "|" << tablero[5] << std::endl;
                std::cout << "-+-+-" << std::endl;
                std::cout << tablero[6] << "|" << tablero[7] << "|" << tablero[8] << std::endl;
                std::cout << "--------------------------" << std::endl;
            }
            else if (tipo == 'T') {
                char s = paquetesRecibidos[0][PROTOCOL_HEADER_LEN]; // Offset ajustado
                if (s == simbolo.load()) {
                    std::cout << "\nEs tu turno (" << simbolo.load() << ")" << std::endl;
                    tuTurno = true;
                } else {
                    std::cout << "\nTurno del oponente (" << s << ")" << std::endl;
                    tuTurno = false;
                }
            }
            else if (tipo == 'O') {
                char resultado = paquetesRecibidos[0][PROTOCOL_HEADER_LEN]; // Offset ajustado
                if (resultado == 'W') {
                    std::cout << "\n¡Has ganado la partida!" << std::endl;
                } else if (resultado == 'L') {
                    std::cout << "\nHas perdido la partida." << std::endl;
                } else if (resultado == 'E') {
                    std::cout << "\nLa partida ha terminado en empate." << std::endl;
                }
                enPartida = false;
                esEspectador = false;
                simbolo = '\0';
                tuTurno = false;
            }
            else if (tipo == 'E') {
                char errCode = paquetesRecibidos[0][PROTOCOL_HEADER_LEN]; // Offset ajustado
                int tamMsgError = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 1, 5)); // Offset ajustado
                std::string msgError = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 1 + 5, tamMsgError); // Offset ajustado
                std::cout << "\nError del servidor (" << errCode << "): " << msgError << std::endl;
            }
            else if (tipo == 'f') { // Recepción de archivo
                std::lock_guard<std::mutex> fileLockGuard(fileMutex); // Proteger operaciones de archivo
                // Liberar el bloqueo de cout mientras el archivo es procesado por fileMutex
                // Esto es necesario porque coutLock (unique_lock) no se destruirá hasta el final del if(completo)
                // y podríamos querer usar cout dentro de la sección protegida por fileMutex.
                coutLock.unlock(); 

                size_t posDatos;
                long lenActualTrozo;
                
                // Determinar las posiciones correctas para la información del archivo
                // El primer fragmento (idx == 0) contiene remitente, nombre de archivo, etc.
                if (idx == 0) { 
                    int tamRem = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN, 5)); // Offset ajustado
                    std::string remitente = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5, tamRem); // Offset ajustado
                    // Se lee como 100 caracteres para el tamaño del nombre de archivo, luego se convierte a int
                    int tamFileName = std::stoi(paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5 + tamRem, 100)); 
                    std::string fileName = paquetesRecibidos[0].substr(PROTOCOL_HEADER_LEN + 5 + tamRem + 100, tamFileName); 
                    
                    posDatos = PROTOCOL_HEADER_LEN + 5 + tamRem + 100 + tamFileName; // Mover puntero al inicio de la longitud del trozo actual
                    lenActualTrozo = std::stol(paquetesRecibidos[0].substr(posDatos, 18)); // Longitud del trozo actual
                    posDatos += 18; // Mover puntero al inicio de los datos del archivo
                    current_hash_expected = std::stoi(paquetesRecibidos[0].substr(posDatos + lenActualTrozo, 5)); // Hash al final del primer trozo

                    std::cout << "\n[DEBUG FILE]: Intentando abrir archivo: '" << fileName << "'" << std::endl; // Imprimir directamente

                    current_output_file.open(fileName, std::ios::binary | std::ios::out | std::ios::trunc); // Crear/truncar archivo directamente en el directorio actual
                    if (!current_output_file.is_open()) {
                        std::cerr << "[ERROR FILE]: No se pudo abrir el archivo para escribir: " << fileName << ". Verifique permisos o ruta." << std::endl; // Imprimir directamente
                        // Restablecer el estado para no seguir esperando fragmentos
                        paquetesRecibidos.clear();
                        expected_total_pkgs = 0;
                        current_file_name.clear();
                        current_hash_expected = 0;
                        total_file_packets = 0;
                        packets_received_count = 0;
                        continue; // No procesar este fragmento ni los siguientes de este archivo
                    }
                    current_file_name = fileName;
                    current_output_file.write(paquetesRecibidos[0].substr(posDatos, lenActualTrozo).c_str(), lenActualTrozo);
                    std::cout << "[DEBUG FILE]: Primer trozo escrito (" << lenActualTrozo << " bytes). Archivo abierto." << std::endl; // Imprimir directamente
                } else { // Fragmentos subsiguientes
                    posDatos = PROTOCOL_HEADER_LEN; // offset para longitud del trozo en subpaquetes
                    lenActualTrozo = std::stol(paquetesRecibidos[idx].substr(posDatos, 18));
                    posDatos += 18; // Mover puntero al inicio de los datos del archivo

                    if (current_output_file.is_open()) {
                        current_output_file.write(paquetesRecibidos[idx].substr(posDatos, lenActualTrozo).c_str(), lenActualTrozo);
                        std::cout << "[DEBUG FILE]: Trozo " << idx + 1 << " escrito (" << lenActualTrozo << " bytes)." << std::endl; // Imprimir directamente
                    } else {
                        std::cerr << "[ERROR FILE]: Intentando escribir en un archivo no abierto para el fragmento subsiguiente. (Archivo: " << current_file_name << ")" << std::endl; // Imprimir directamente
                        paquetesRecibidos.clear();
                        expected_total_pkgs = 0;
                        current_file_name.clear();
                        current_hash_expected = 0;
                        total_file_packets = 0;
                        packets_received_count = 0;
                        continue;
                    }
                }

                if (seq == 0 && current_output_file.is_open()) { // Último fragmento (seq=0 indica el último)
                    std::cout << "[DEBUG FILE]: Ultimo fragmento recibido. Cerrando archivo: " << current_file_name << std::endl; // Imprimir directamente
                    current_output_file.close();
                    
                    // Calcular hash del archivo recibido
                    std::ifstream received_file(current_file_name, std::ios::binary | std::ios::ate);
                    if (!received_file.is_open()) {
                        std::cerr << "[ERROR FILE]: No se pudo reabrir el archivo recibido para verificacion de hash. Eliminando archivo incompleto." << std::endl; // Imprimir directamente
                        std::remove(current_file_name.c_str()); // Eliminar archivo si no se pudo abrir para verificar
                    } else {
                        long file_size = received_file.tellg();
                        received_file.seekg(0, std::ios::beg);
                        std::vector<char> file_data(file_size); // Usar vector para evitar gestión manual de memoria
                        received_file.read(file_data.data(), file_size);
                        received_file.close();

                        int calculated_hash = calcularHash(file_data.data(), file_size);

                        if (calculated_hash == current_hash_expected) {
                            std::cout << "[DEBUG FILE]: Archivo '" << current_file_name << "' recibido completamente y verificado. Hash: " << calculated_hash << std::endl; // Imprimir directamente
                        } else {
                            std::cerr << "[ERROR FILE]: El archivo '" << current_file_name << "' se recibio, pero el hash no coincide. Esperado: " << current_hash_expected << ", Calculado: " << calculated_hash << ". Eliminando archivo corrupto." << std::endl; // Imprimir directamente
                            std::remove(current_file_name.c_str()); // Eliminar archivo si el hash no coincide
                        }
                    }
                    current_file_name.clear();
                    current_hash_expected = 0;
                    total_file_packets = 0;
                    packets_received_count = 0;
                }
                // Después de todas las operaciones de archivo (protegidas por fileMutex),
                // readquirir el coutLock para que el resto del bloque `if (completo)`
                // y los mensajes posteriores al menú funcionen correctamente.
                coutLock.lock();
            }
            
            // Si llegamos aquí, el mensaje (o archivo) fue completamente reensamblado.
            // Limpiamos los paquetes recibidos y el total esperado para la próxima secuencia.
            paquetesRecibidos.clear();
            expected_total_pkgs = 0;
        }
    }
    close(sock);
}

void menuPrincipal(int socketCliente) {
    // Offset para el tipo de mensaje y datos cambia de 11 a 15 debido a 5 dígitos para seq y tot
    const int PROTOCOL_HEADER_LEN = 15; // 5 (seq) + 5 (tot) + 5 (len) + 1 (tipo)

    while (true) {
        std::string opcionStr;
        char opcion;

        std::lock_guard<std::mutex> lock(coutMux); // Proteger cout
        if (!enPartida && !esEspectador) {
            std::cout << "\nSelecciona una opcion:" << std::endl;
            std::cout << "l. Listar usuarios" << std::endl;
            std::cout << "m. Enviar mensaje privado" << std::endl;
            std::cout << "b. Enviar mensaje broadcast" << std::endl;
            std::cout << "q. Salir" << std::endl;
            std::cout << "j. Jugar Tres en Raya" << std::endl;
            std::cout << "v. Ver partida de Tres en Raya" << std::endl;
            std::cout << "f. Enviar archivo" << std::endl;
            std::cout << "Opcion: ";
        } else if (enPartida && !esEspectador) {
            std::cout << "\nEstas en una partida de Tres en Raya. Tu simbolo es: " << simbolo.load() << std::endl;
            std::cout << "Selecciona una opcion:" << std::endl;
            if (tuTurno) {
                 std::cout << "p. Poner tu ficha" << std::endl;
            } else {
                std::cout << "(Esperando tu turno)" << std::endl;
            }
            std::cout << "r. Rendirse" << std::endl;
            std::cout << "Opcion: ";
        } else if (esEspectador) {
            std::cout << "\nEstas en modo espectador." << std::endl;
            std::cout << "Selecciona una opcion:" << std::endl;
            std::cout << "s. Dejar de ver la partida" << std::endl;
            std::cout << "Opcion: ";
        }
        std::cin >> opcion;
        clearInputBuffer(); // Limpiar buffer de entrada
        
        if (std::cin.fail()) {
            std::cin.clear();
            clearInputBuffer();
            std::cout << "Entrada invalida. Intentalo de nuevo." << std::endl;
            continue;
        }

        char buffer[500];
        memset(buffer, '#', 500);

        switch (opcion) {
            case 'l': { // Mensaje L
                strcpy(buffer, "000010000100001l"); // 5 dígitos para seq y tot
                write(socketCliente, buffer, 500);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                break;
            }
            case 'm': { // Mensaje M
                std::string destino, mensaje;
                std::cout << "Destino: ";
                std::getline(std::cin, destino);
                std::cout << "Mensaje: ";
                std::getline(std::cin, mensaje);

                int sizeFijo = 1 + 5 + (int)mensaje.length() + 5 + (int)destino.length(); // Tipo + TamMsg + Msg + TamDest + Dest
                int tamMaxPayload = 500 - PROTOCOL_HEADER_LEN - (sizeFijo - (int)mensaje.length()); // Restar solo el tamaño de mensaje del total
                
                std::vector<std::string> piezas = (mensaje.length() > tamMaxPayload) ? 
                    partir(mensaje, tamMaxPayload) : std::vector<std::string>{mensaje};
                
                int totalPkg = piezas.size();
                int seq = 1;

                for (auto &trozo : piezas) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // 5 dígitos para seq y tot
                    int currentTotalSize = 1 + 5 + (int)trozo.length() + 5 + (int)destino.length();

                    int offset = 10; // Offset ajustado
                    sprintf(buffer + offset, "%05d", currentTotalSize);
                    offset += 5;
                    buffer[offset++] = 'm';

                    sprintf(buffer + offset, "%05d", (int)trozo.length());
                    offset += 5;
                    memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();

                    sprintf(buffer + offset, "%05d", (int)destino.length());
                    offset += 5;
                    memcpy(buffer + offset, destino.c_str(), destino.length());

                    write(socketCliente, buffer, 500);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    seq++;
                }
                break;
            }
            case 'b': { // Mensaje B
                std::string mensaje;
                std::cout << "Mensaje broadcast: ";
                std::getline(std::cin, mensaje);

                int sizeFijo = 1 + 5 + (int)mensaje.length() + 5 + (int)nickname.length();
                int tamMaxPayload = 500 - PROTOCOL_HEADER_LEN - (sizeFijo - (int)mensaje.length());
                
                std::vector<std::string> piezas = (mensaje.length() > tamMaxPayload) ? 
                    partir(mensaje, tamMaxPayload) : std::vector<std::string>{mensaje};
                
                int totalPkg = piezas.size();
                int seq = 1;

                for (auto &trozo : piezas) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // 5 dígitos para seq y tot
                    int currentTotalSize = 1 + 5 + (int)trozo.length() + 5 + (int)nickname.length();

                    int offset = 10; // Offset ajustado
                    sprintf(buffer + offset, "%05d", currentTotalSize);
                    offset += 5;
                    buffer[offset++] = 'b';

                    sprintf(buffer + offset, "%05d", (int)trozo.length());
                    offset += 5;
                    memcpy(buffer + offset, trozo.c_str(), trozo.length());
                    offset += trozo.length();

                    sprintf(buffer + offset, "%05d", (int)nickname.length());
                    offset += 5;
                    memcpy(buffer + offset, nickname.c_str(), nickname.length());

                    write(socketCliente, buffer, 500);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    seq++;
                }
                break;
            }
            case 'q': { // Mensaje Q
                strcpy(buffer, "000010000100001Q"); // 5 dígitos para seq y tot
                write(socketCliente, buffer, 500);
                return;
            }
            case 'j': { // Mensaje J (jugar)
                if (!enPartida && !esEspectador) {
                    memset(buffer, '#', 500);
                    strcpy(buffer, "000010000100001J"); // 5 dígitos para seq y tot
                    write(socketCliente, buffer, 500);
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Ya estas en partida o en modo espectador." << std::endl;
                }
                break;
            }
            case 'v': { // Mensaje V (ver partida)
                if (!enPartida && !esEspectador) {
                    memset(buffer, '#', 500);
                    strcpy(buffer, "000010000100001V"); // 5 dígitos para seq y tot
                    write(socketCliente, buffer, 500);
                    esEspectador = true;
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Entrando en modo espectador..." << std::endl;
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Ya estas en partida o en modo espectador." << std::endl;
                }
                break;
            }
            case 'p': { // Mensaje P (jugar - poner ficha)
                if (enPartida && tuTurno) {
                    int pos;
                    std::cout << "Ingresa la posicion (1-9): ";
                    std::cin >> pos;
                    clearInputBuffer();

                    if (std::cin.fail() || pos < 1 || pos > 9) {
                        std::cin.clear();
                        clearInputBuffer();
                        std::lock_guard<std::mutex> lock(coutMux);
                        std::cout << "Posicion invalida. Ingresa un numero del 1 al 9." << std::endl;
                        break;
                    }
                    
                    char posChar = (char)(pos + '0');

                    memset(buffer, '#', 500);
                    sprintf(buffer, "000010000100003P%c%c", posChar, simbolo.load()); // 5 dígitos para seq y tot
                    write(socketCliente, buffer, 500);
                    tuTurno = false;
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "No es tu turno o no estas en una partida activa para mover." << std::endl;
                }
                break;
            }
            case 'r': { // Mensaje R (rendirse)
                if (enPartida && !esEspectador) {
                    memset(buffer, '#', 500);
                    strcpy(buffer, "000010000100001R"); // 5 dígitos para seq y tot
                    write(socketCliente, buffer, 500);
                    enPartida = false;
                    esEspectador = false;
                    simbolo = '\0';
                    tuTurno = false;
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Te has rendido. La partida ha terminado." << std::endl;
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "No estas en una partida activa para rendirte." << std::endl;
                }
                break;
            }
            case 's': { // Mensaje S (salir de espectador)
                if (esEspectador) {
                    memset(buffer, '#', 500);
                    strcpy(buffer, "000010000100001S"); // 5 dígitos para seq y tot
                    write(socketCliente, buffer, 500);
                    esEspectador = false;
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Has salido del modo espectador." << std::endl;
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "No estas en modo espectador." << std::endl;
                }
                break;
            }
            case 'f': { // Mensaje F (enviar archivo)
                std::string destino, filePath;
                std::cout << "Destino: ";
                std::getline(std::cin, destino);
                std::cout << "Ruta del archivo a enviar: ";
                std::getline(std::cin, filePath);

                std::ifstream file(filePath, std::ios::binary | std::ios::ate);
                if (!file.is_open()) {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Error: No se pudo abrir el archivo de origen '" << filePath << "'." << std::endl;
                    break;
                }

                long fileSize = file.tellg();
                file.seekg(0, std::ios::beg);

                // Leer todo el archivo en un buffer
                std::vector<char> fileBuffer(fileSize);
                file.read(fileBuffer.data(), fileSize);
                file.close();

                int fileHash = calcularHash(fileBuffer.data(), fileSize);

                // Extraer solo el nombre del archivo de la ruta
                size_t lastSlash = filePath.find_last_of("/\\");
                std::string fileName = (lastSlash == std::string::npos) ? filePath : filePath.substr(lastSlash + 1);

                // Calcular el tamaño máximo de payload para el primer fragmento
                // PROTOCOL_HEADER_LEN es 15.
                // 1 byte de tipo 'f'
                // 5 bytes de longitud de remitente + longitud de remitente (nickname del cliente)
                // 100 bytes para longitud de nombre de archivo (padding incluido) + longitud de nombre de archivo
                // 18 bytes para longitud de trozo de archivo + 5 bytes para hash
                int headerPrimerFragmentoExcluyendoData = 1 + 5 + (int)nickname.length() + 100 + (int)fileName.length() + 18 + 5;
                int tamMaxPrimerFragmentoData = 500 - PROTOCOL_HEADER_LEN - headerPrimerFragmentoExcluyendoData;

                // Calcular el tamaño máximo de payload para los fragmentos subsiguientes
                // PROTOCOL_HEADER_LEN es 15.
                // 1 byte de tipo 'f'
                // 18 bytes para longitud de trozo de archivo + 5 bytes para hash
                int headerSubFragmentoExcluyendoData = 1 + 18 + 5;
                int tamMaxSubFragmentoData = 500 - PROTOCOL_HEADER_LEN - headerSubFragmentoExcluyendoData;

                std::vector<std::string> piezas_archivo;
                
                // Dividir el archivo en fragmentos
                if (fileSize > tamMaxPrimerFragmentoData) {
                    // Primer fragmento
                    piezas_archivo.push_back(std::string(fileBuffer.begin(), fileBuffer.begin() + tamMaxPrimerFragmentoData));
                    
                    // Fragmentos subsiguientes
                    for (long i = tamMaxPrimerFragmentoData; i < fileSize; i += tamMaxSubFragmentoData) {
                        piezas_archivo.push_back(std::string(fileBuffer.begin() + i, fileBuffer.begin() + std::min((long)fileBuffer.size(), i + tamMaxSubFragmentoData)));
                    }
                } else {
                    // Si el archivo cabe en el primer fragmento
                    piezas_archivo.push_back(std::string(fileBuffer.begin(), fileBuffer.end()));
                }

                int totalPkg = piezas_archivo.size();

                // *********** VERIFICACIÓN CRÍTICA ***********
                // Asegurarse de que el número total de paquetes no exceda 5 dígitos (99999)
                if (totalPkg > 99999) {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cerr << "Error: El archivo es demasiado grande. Excede el limite de 99999 fragmentos. (Fragmentos calculados: " << totalPkg << ")" << std::endl;
                    break; 
                }
                // *******************************************

                int seq = 1;

                for (const auto& trozo : piezas_archivo) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // 5 dígitos para seq y tot
                    
                    int currentPayloadSize; // El tamaño de los datos específicos del mensaje (después del tipo)
                    int offset = 10; // Offset para el campo de tamaño total de payload (len)
                    
                    if (seq == 1) { // Primer fragmento
                        currentPayloadSize = 1 + 5 + (int)nickname.length() + 100 + (int)fileName.length() +
                                            18 + (int)trozo.length() + 5; // Tipo 'f' + lenRem + Rem + lenFileName + FileName + lenTrozo + Trozo + Hash
                        sprintf(buffer + offset, "%05d", currentPayloadSize);
                        offset += 5;
                        buffer[offset++] = 'f'; // Tipo 'f' para archivo
                        
                        sprintf(buffer + offset, "%05d", (int)nickname.length()); // Longitud del remitente (nuestro nickname)
                        offset += 5;
                        memcpy(buffer + offset, nickname.c_str(), nickname.length());
                        offset += nickname.length();

                        sprintf(buffer + offset, "%0100d", (int)fileName.length()); // Longitud del nombre del archivo (con padding)
                        offset += 100;
                        memcpy(buffer + offset, fileName.c_str(), fileName.length());
                        offset += fileName.length();
                        
                        sprintf(buffer + offset, "%018ld", trozo.length()); // Longitud del trozo actual
                        offset += 18;
                        memcpy(buffer + offset, trozo.c_str(), trozo.length());
                        offset += trozo.length();

                        sprintf(buffer + offset, "%05d", fileHash); // Hash del archivo completo como 5 dígitos
                    } else { // Fragmentos subsiguientes
                        currentPayloadSize = 1 + 18 + (int)trozo.length() + 5; // Tipo 'f' + lenTrozo + Trozo + Hash
                        sprintf(buffer + offset, "%05d", currentPayloadSize);
                        offset += 5;
                        buffer[offset++] = 'f'; // Tipo 'f' para archivo

                        sprintf(buffer + offset, "%018ld", trozo.length()); // Longitud del trozo actual
                        offset += 18;
                        memcpy(buffer + offset, trozo.c_str(), trozo.length());
                        offset += trozo.length();

                        sprintf(buffer + offset, "%05d", fileHash); // Hash del archivo completo como 5 dígitos
                    }
                    write(socketCliente, buffer, 500);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    ++seq;
                }
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "Archivo '" << fileName << "' enviado a " << destino << ". Esperando confirmacion..." << std::endl;
                // La interfaz no volverá al menú principal hasta que se reciba el ACK de archivo
                break;
            }
            default: {
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "Opcion invalida. Intentalo de nuevo." << std::endl;
                break;
            }
        }
    }
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
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sock);
        exit(1);
    }
    
    socketGlobal = sock;

    std::cout << "Conectado al servidor." << std::endl;

    std::cout << "Ingresa tu nickname: ";
    std::getline(std::cin, nickname);
    
    enviarNickname(sock);

    std::thread hiloRecepcion(recibirMensajes, sock);
    menuPrincipal(sock);

    hiloRecepcion.join();
    close(sock);
    return 0;
}