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

// --- Protocol Constants ---
const int SEQ_LEN = 5;
const int TOT_LEN = 5;
const int LEN_FIELD_LEN = 5; // Length of the length field itself
const int TYPE_LEN = 1;
const int FRAGMENTATION_HEADER_LEN = SEQ_LEN + TOT_LEN; // 10 bytes for seq and tot
const int PROTOCOL_HEADER_LEN = FRAGMENTATION_HEADER_LEN + LEN_FIELD_LEN + TYPE_LEN; // 10 + 5 + 1 = 16 bytes

// calcular hash (suma de bytes mod 100000)
int calcularHash(const char *datos, long tamano) {
    int hash = 0;
    for (long i = 0; i < tamano; i++) {
        hash = (hash + (unsigned char)datos[i]) % 100000;
    }
    return hash;
}

std::vector<std::string> partir(const std::string& msg, int tamMax) {
    std::vector<std::string> partes;
    int len = msg.size();

    for (int i = 0; i < len; i += tamMax) {
        partes.push_back(msg.substr(i, tamMax));
    }

    return partes;
}

void enviarM(int sock, const std::string &msg) {
    const std::string remitente = "servidor"; // Mensajes del servidor no tienen remitente "real"
    int lenMsg = msg.size();
    int lenRem = remitente.size();
    int tamTot = TYPE_LEN + LEN_FIELD_LEN + lenMsg + LEN_FIELD_LEN + lenRem; // 'm' + len_msg + msg + len_rem + rem

    char buffer[500];
    memset(buffer, '#', 500);
    
    // Fragmento 1 de 1
    sprintf(buffer, "%05d%05d", 1, 1); // seq, tot

    int offset = FRAGMENTATION_HEADER_LEN; // Start after seq and tot
    sprintf(buffer + offset, "%05d", tamTot); // total size of the message payload
    offset += LEN_FIELD_LEN;
    
    buffer[offset++] = 'm'; // Type
    
    sprintf(buffer + offset, "%05d", lenMsg); // Length of message content
    offset += LEN_FIELD_LEN;
    memcpy(buffer + offset, msg.c_str(), lenMsg);
    offset += lenMsg;
    
    sprintf(buffer + offset, "%05d", lenRem); // Length of sender nickname
    offset += LEN_FIELD_LEN;
    memcpy(buffer + offset, remitente.c_str(), lenRem);
    
    write(sock, buffer, 500);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void recibirMensajes(int sock) {
    char buffer[500];
    std::vector<std::string> vc;
    int expected_total_pkgs = 0;
    int current_message_number = 0; // For client side message numbering

    while (true) {
        int n = read(sock, buffer, 500);
        if (n <= 0) {
            if (n == 0) {
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "\nDesconectado del servidor." << std::endl;
            } else {
                std::lock_guard<std::mutex> lock(coutMux);
                perror("read error");
            }
            break;
        }

        std::string pkg(buffer, n);

        if (pkg.length() < FRAGMENTATION_HEADER_LEN) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Cliente: Error: Paquete demasiado corto para cabecera de fragmentacion." << std::endl;
            continue;
        }

        int seq = std::stoi(pkg.substr(0, SEQ_LEN));
        int tot = std::stoi(pkg.substr(SEQ_LEN, TOT_LEN));

        if (seq < 0 || seq > tot || tot <= 0) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Cliente: Error de fragmento: Valores de secuencia/total invalidos (seq=" << seq << ", tot=" << tot << "). Reiniciando reensamblaje." << std::endl;
            vc.clear();
            expected_total_pkgs = 0;
            continue;
        }
        
        int idx = (seq == 0) ? tot - 1 : seq - 1; // 0 means the last package, its index is tot-1

        if (expected_total_pkgs == 0) {
            expected_total_pkgs = tot;
            vc.resize(tot);
            current_message_number++;
            std::lock_guard<std::mutex> lock(coutMux);
            std::cout << "Cliente: Recibiendo mensaje #" << current_message_number << std::endl;
        } else if (tot != expected_total_pkgs) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Cliente: Error de fragmento: El numero total de paquetes cambio (esperado "
                      << expected_total_pkgs << ", recibido " << tot << "). Reiniciando reensamblaje." << std::endl;
            vc.clear();
            expected_total_pkgs = tot;
            vc.resize(tot);
            current_message_number++; // Consider as a new message
            std::cout << "Cliente: Recibiendo mensaje #" << current_message_number << std::endl;
        }

        if (idx < 0 || idx >= vc.size()) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Cliente: Error de fragmento: Indice fuera de rango o invalido (idx=" << idx << ", vc.size=" << vc.size() << ")." << std::endl;
            continue;
        }
        
        if (!vc[idx].empty()) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cerr << "Cliente: Advertencia: Fragmento " << idx + 1 << " ya recibido para mensaje #" << current_message_number << ". Ignorando duplicado o error." << std::endl;
            continue;
        }

        vc[idx] = std::move(pkg);
        std::lock_guard<std::mutex> lock(coutMux);
        std::cout << "Cliente: Recibiendo fragmento " << idx + 1 << " de " << expected_total_pkgs << " del mensaje #" << current_message_number << std::endl;

        bool completo = true;
        for (int i = 0; i < expected_total_pkgs; ++i) {
            if (vc[i].empty()) {
                completo = false;
                break;
            }
        }

        if (completo) {
            char tipo = vc[0][PROTOCOL_HEADER_LEN]; // Type is at index 15
            std::vector<std::string> paquetesCompletos = std::move(vc);
            vc.clear();
            expected_total_pkgs = 0;

            // Process the complete message
            if (tipo == 'l') { // Lista de usuarios
                // PROTOCOL_HEADER_LEN is 16. len_field (5 chars) after that.
                // Data starts at PROTOCOL_HEADER_LEN + LEN_FIELD_LEN = 16 + 5 = 21
                int data_start_offset = PROTOCOL_HEADER_LEN + LEN_FIELD_LEN;
                int len_users = std::stoi(paquetesCompletos[0].substr(data_start_offset - LEN_FIELD_LEN, LEN_FIELD_LEN));
                std::string usuarios = paquetesCompletos[0].substr(data_start_offset, len_users);
                for(size_t i = 1; i < paquetesCompletos.size(); ++i) {
                    usuarios += paquetesCompletos[i].substr(FRAGMENTATION_HEADER_LEN); // Only seq+tot, rest is data
                }
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "Usuarios conectados: " << usuarios << std::endl;
            } else if (tipo == 'm' || tipo == 'b') { // Mensaje privado o broadcast
                int data_start_offset = PROTOCOL_HEADER_LEN + LEN_FIELD_LEN; // len_msg + msg
                int len_msg_content = std::stoi(paquetesCompletos[0].substr(data_start_offset - LEN_FIELD_LEN, LEN_FIELD_LEN));
                std::string mensaje_content = paquetesCompletos[0].substr(data_start_offset, len_msg_content);

                for (size_t i = 1; i < paquetesCompletos.size(); ++i) {
                    // For subsequent fragments, the content starts after seq(5) + tot(5) + len(5)
                    // But the len field in the sub-fragment header only contains the length of that sub-fragment's payload.
                    // So we need to re-parse the actual length for each fragment, or rely on the fact that
                    // 'partir' already handled this and we just need the data after the initial protocol header.
                    int sub_fragment_data_start_offset = FRAGMENTATION_HEADER_LEN + LEN_FIELD_LEN;
                    int sub_len_content = std::stoi(paquetesCompletos[i].substr(FRAGMENTATION_HEADER_LEN, LEN_FIELD_LEN));
                    mensaje_content += paquetesCompletos[i].substr(sub_fragment_data_start_offset, sub_len_content);
                }

                int len_rem_start = data_start_offset + len_msg_content; // Correct position for len_rem in first pkg
                if (paquetesCompletos.size() > 1) { // If it was fragmented
                    // The last fragment will contain the sender info
                    // Calculate start of sender info in the last fragment
                    len_rem_start = PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + std::stoi(paquetesCompletos.back().substr(FRAGMENTATION_HEADER_LEN, LEN_FIELD_LEN));
                }

                int len_rem = std::stoi(paquetesCompletos.back().substr(len_rem_start, LEN_FIELD_LEN));
                std::string remitente = paquetesCompletos.back().substr(len_rem_start + LEN_FIELD_LEN, len_rem);

                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "[Mensaje de " << remitente << "]: " << mensaje_content << std::endl;
            }
            else if (tipo == 'A') { // ACK de archivo
                // Format: A + S/F + mensaje
                // len_field (5 chars) after PROTOCOL_HEADER_LEN. Data starts at PROTOCOL_HEADER_LEN + LEN_FIELD_LEN.
                int data_start_offset = PROTOCOL_HEADER_LEN + LEN_FIELD_LEN;
                int len_ack_data = std::stoi(paquetesCompletos[0].substr(PROTOCOL_HEADER_LEN, LEN_FIELD_LEN));
                char resultado = paquetesCompletos[0][data_start_offset];
                std::string mensaje_ack = paquetesCompletos[0].substr(data_start_offset + TYPE_LEN, len_ack_data - TYPE_LEN);
                std::lock_guard<std::mutex> lock(coutMux);
                if (resultado == 'S') {
                    std::cout << "Confirmacion de archivo: " << mensaje_ack << std::endl;
                } else {
                    std::cerr << "Fallo en envio de archivo: " << mensaje_ack << std::endl;
                }
            }
            else if (tipo == 'f') { // Recepción de archivo
                // Structure for file fragments:
                // First fragment: [seq(5)][tot(5)][len_total_payload(5)]'f'[len_rem(5)][rem(N)][len_filename(100)][filename(M)][len_file_chunk(18)][file_chunk(X)][hash(5)]
                // Subsequent fragments: [seq(5)][tot(5)][len_total_payload(5)]'f'[len_file_chunk(18)][file_chunk(Y)][hash(5)]

                std::string file_content;
                std::string sender_nick;
                std::string received_filename;
                int received_hash = -1;
                long total_file_size = 0; // Not strictly used for reassembly, but for info

                for (size_t i = 0; i < paquetesCompletos.size(); ++i) {
                    const std::string& current_pkg = paquetesCompletos[i];
                    int current_pkg_data_offset = PROTOCOL_HEADER_LEN; // After seq, tot, len, type

                    if (i == 0) {
                        // First fragment
                        int len_rem = std::stoi(current_pkg.substr(current_pkg_data_offset, LEN_FIELD_LEN));
                        sender_nick = current_pkg.substr(current_pkg_data_offset + LEN_FIELD_LEN, len_rem);
                        current_pkg_data_offset += LEN_FIELD_LEN + len_rem;

                        int len_filename = std::stoi(current_pkg.substr(current_pkg_data_offset, 100)); // 100 for filename length
                        received_filename = current_pkg.substr(current_pkg_data_offset + 100, len_filename);
                        current_pkg_data_offset += 100 + len_filename;
                        
                        long len_chunk = std::stol(current_pkg.substr(current_pkg_data_offset, 18)); // 18 for chunk length
                        file_content += current_pkg.substr(current_pkg_data_offset + 18, len_chunk);
                        current_pkg_data_offset += 18 + len_chunk;

                        received_hash = std::stoi(current_pkg.substr(current_pkg_data_offset, 5)); // 5 for hash
                    } else {
                        // Subsequent fragments
                        long len_chunk = std::stol(current_pkg.substr(current_pkg_data_offset, 18)); // 18 for chunk length
                        file_content += current_pkg.substr(current_pkg_data_offset + 18, len_chunk);
                        
                        received_hash = std::stoi(current_pkg.substr(current_pkg_data_offset + 18 + len_chunk, 5)); // 5 for hash
                    }
                }

                int calculated_hash = calcularHash(file_content.c_str(), file_content.length());
                
                std::lock_guard<std::mutex> lock_cout(coutMux);
                std::cout << "Archivo '" << received_filename << "' recibido de '" << sender_nick << "'" << std::endl;
                std::cout << "Hash recibido: " << received_hash << ", Hash calculado: " << calculated_hash << std::endl;

                if (calculated_hash == received_hash) {
                    std::lock_guard<std::mutex> lock_file(fileMutex); // Protect file writing
                    std::string path = "archivos_recibidos/" + received_filename;
                    std::ofstream outfile(path, std::ios::binary);
                    if (outfile.is_open()) {
                        outfile.write(file_content.c_str(), file_content.length());
                        outfile.close();
                        std::cout << "Archivo guardado exitosamente como: " << path << std::endl;
                        enviarM(sock, "Archivo '" + received_filename + "' recibido y guardado exitosamente. Hash OK.");
                    } else {
                        std::cerr << "Error al abrir/crear archivo para guardar: " << path << std::endl;
                        enviarM(sock, "Error al guardar archivo '" + received_filename + "'.");
                    }
                } else {
                    std::cerr << "Error: El hash del archivo recibido no coincide. Archivo corrupto." << std::endl;
                    enviarM(sock, "Error: El hash del archivo '" + received_filename + "' no coincide. Archivo corrupto.");
                }

            }
            else if (tipo == 'E') { // Errores generales
                char error_code = paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN]; // Error code after len field
                int error_msg_len = std::stoi(paquetesCompletos[0].substr(PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + TYPE_LEN, LEN_FIELD_LEN));
                std::string error_message = paquetesCompletos[0].substr(PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + TYPE_LEN + LEN_FIELD_LEN, error_msg_len);
                std::lock_guard<std::mutex> lock(coutMux);
                std::cerr << "Error " << error_code << ": " << error_message << std::endl;
                if (error_code == '1') { // Nickname ya en uso
                    envLista = false; // Permite intentar registrarse de nuevo
                }
            } else if (tipo == 'X') { // Tablero de juego
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "\n--- Tablero ---" << std::endl;
                std::cout << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 0] << "|"
                          << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 1] << "|"
                          << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 2] << std::endl;
                std::cout << "-----" << std::endl;
                std::cout << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 3] << "|"
                          << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 4] << "|"
                          << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 5] << std::endl;
                std::cout << "-----" << std::endl;
                std::cout << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 6] << "|"
                          << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 7] << "|"
                          << paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 8] << std::endl;
                std::cout << "---------------" << std::endl;
            } else if (tipo == 'T') { // Turno
                tuTurno = true;
                simbolo = paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN];
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "Es tu turno! Juegas con: " << simbolo << std::endl;
            } else if (tipo == 'O') { // Fin de partida
                enPartida = false;
                esEspectador = false;
                tuTurno = false;
                char resultado = paquetesCompletos[0][PROTOCOL_HEADER_LEN + LEN_FIELD_LEN];
                std::lock_guard<std::mutex> lock(coutMux);
                if (resultado == 'W') {
                    std::cout << "Has ganado la partida!" << std::endl;
                } else if (resultado == 'L') {
                    std::cout << "Has perdido la partida." << std::endl;
                } else {
                    std::cout << "La partida ha terminado en empate." << std::endl;
                }
            } else {
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "Tipo de mensaje desconocido: " << tipo << std::endl;
            }
        }
    }
    close(sock);
}

void mostrarMenu(int sock) {
    char opcion;
    std::string mensaje;
    std::string destino;
    char buffer[500];

    while (true) {
        if (!envLista) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cout << "Ingresa tu nickname: ";
            std::cin >> nickname;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Limpiar buffer

            // Enviar N (registro de nickname)
            int tamTot = TYPE_LEN + LEN_FIELD_LEN + nickname.length(); // 'N' + len_nick + nick
            memset(buffer, '#', 500);
            sprintf(buffer, "%05d%05d", 1, 1); // seq, tot
            int offset = FRAGMENTATION_HEADER_LEN;
            sprintf(buffer + offset, "%05d", tamTot); // len
            offset += LEN_FIELD_LEN;
            buffer[offset++] = 'N'; // tipo
            sprintf(buffer + offset, "%05d", (int)nickname.length());
            offset += LEN_FIELD_LEN;
            memcpy(buffer + offset, nickname.c_str(), nickname.length());

            write(sock, buffer, 500);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            envLista = true; // Asumimos que se envio, el servidor confirmara
            continue;
        }

        if (preguntaVer) {
            std::lock_guard<std::mutex> lock(coutMux);
            std::cout << "¿Quieres unirte como espectador? (s/n): ";
            char res;
            std::cin >> res;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (res == 's' || res == 'S') {
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", 1, 1); // seq, tot
                int offset = FRAGMENTATION_HEADER_LEN;
                sprintf(buffer + offset, "%05d", 1); // len: 1 for 'V'
                offset += LEN_FIELD_LEN;
                buffer[offset++] = 'V'; // tipo
                write(sock, buffer, 500);
                esEspectador = true;
                enPartida = true;
            } else {
                std::cout << "No te uniste como espectador." << std::endl;
            }
            preguntaVer = false;
        }

        std::lock_guard<std::mutex> lock(coutMux);
        std::cout << "\n--- Menu ---" << std::endl;
        std::cout << "1. Listar usuarios" << std::endl;
        std::cout << "2. Enviar mensaje privado" << std::endl;
        std::cout << "3. Enviar mensaje broadcast" << std::endl;
        std::cout << "4. Enviar archivo" << std::endl;
        std::cout << "5. Unirse a partida de Tic-Tac-Toe" << std::endl;
        if (enPartida && tuTurno) {
            std::cout << "6. Realizar jugada (Tic-Tac-Toe)" << std::endl;
        }
        std::cout << "7. Salir" << std::endl;
        std::cout << "Opcion: ";
        std::cin >> opcion;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Limpiar buffer

        switch (opcion) {
            case '1': { // Listar usuarios 'L'
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", 1, 1); // seq, tot
                int offset = FRAGMENTATION_HEADER_LEN;
                sprintf(buffer + offset, "%05d", 1); // len: 1 for 'L'
                offset += LEN_FIELD_LEN;
                buffer[offset++] = 'L'; // tipo
                write(sock, buffer, 500);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                break;
            }
            case '2': { // Mensaje privado 'M'
                std::cout << "Destinatario: ";
                std::getline(std::cin, destino);
                std::cout << "Mensaje: ";
                std::getline(std::cin, mensaje);

                // Calculate max payload size for message content + sender nickname + their lengths
                int max_data_payload_per_fragment = 500 - PROTOCOL_HEADER_LEN; // Max data per fragment after protocol header
                int initial_fragment_overhead = LEN_FIELD_LEN + destino.length() + LEN_FIELD_LEN + nickname.length();
                int subsequent_fragment_overhead = LEN_FIELD_LEN; // Only message content length for subsequent fragments

                std::vector<std::string> piezas;
                if (mensaje.length() + initial_fragment_overhead <= max_data_payload_per_fragment) {
                    piezas.push_back(mensaje); // No fragmentation needed for message content
                } else {
                    int first_chunk_size = max_data_payload_per_fragment - initial_fragment_overhead;
                    piezas.push_back(mensaje.substr(0, first_chunk_size));
                    std::string remaining_msg = mensaje.substr(first_chunk_size);
                    int max_sub_chunk_size = max_data_payload_per_fragment - subsequent_fragment_overhead;
                    std::vector<std::string> sub_pieces = partir(remaining_msg, max_sub_chunk_size);
                    piezas.insert(piezas.end(), sub_pieces.begin(), sub_pieces.end());
                }

                int totalPkg = piezas.size();
                int seq = 1;

                for (const auto& trozo : piezas) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // seq, tot

                    int current_payload_len;
                    int current_offset = FRAGMENTATION_HEADER_LEN;

                    if (seq == 1) { // First fragment includes full message header (type, msg_len, msg, dest_len, dest)
                        current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length() + LEN_FIELD_LEN + destino.length();
                        sprintf(buffer + current_offset, "%05d", current_payload_len); // len
                        current_offset += LEN_FIELD_LEN;
                        buffer[current_offset++] = 'M'; // tipo
                        sprintf(buffer + current_offset, "%05d", (int)trozo.length()); // len_msg
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // msg
                        current_offset += trozo.length();
                        sprintf(buffer + current_offset, "%05d", (int)destino.length()); // len_dest
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, destino.c_str(), destino.length()); // dest
                    } else { // Subsequent fragments only need type, msg_len, msg
                        current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length();
                        sprintf(buffer + current_offset, "%05d", current_payload_len); // len
                        current_offset += LEN_FIELD_LEN;
                        buffer[current_offset++] = 'M'; // tipo
                        sprintf(buffer + current_offset, "%05d", (int)trozo.length()); // len_msg
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // msg
                    }

                    write(sock, buffer, 500);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Enviando fragmento " << seq << " de " << totalPkg << " (Mensaje Privado)..." << std::endl;
                    seq++;
                }
                break;
            }
            case '3': { // Mensaje broadcast 'B'
                std::cout << "Mensaje para todos: ";
                std::getline(std::cin, mensaje);
                
                int max_data_payload_per_fragment = 500 - PROTOCOL_HEADER_LEN; // Max data per fragment after protocol header
                int initial_fragment_overhead = LEN_FIELD_LEN + nickname.length(); // len_rem + rem
                int subsequent_fragment_overhead = LEN_FIELD_LEN; // Only message content length for subsequent fragments

                std::vector<std::string> piezas;
                if (mensaje.length() + initial_fragment_overhead <= max_data_payload_per_fragment) {
                    piezas.push_back(mensaje); // No fragmentation needed for message content
                } else {
                    int first_chunk_size = max_data_payload_per_fragment - initial_fragment_overhead;
                    piezas.push_back(mensaje.substr(0, first_chunk_size));
                    std::string remaining_msg = mensaje.substr(first_chunk_size);
                    int max_sub_chunk_size = max_data_payload_per_fragment - subsequent_fragment_overhead;
                    std::vector<std::string> sub_pieces = partir(remaining_msg, max_sub_chunk_size);
                    piezas.insert(piezas.end(), sub_pieces.begin(), sub_pieces.end());
                }

                int totalPkg = piezas.size();
                int seq = 1;

                for (const auto& trozo : piezas) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // seq, tot

                    int current_payload_len;
                    int current_offset = FRAGMENTATION_HEADER_LEN;

                    if (seq == 1) { // First fragment includes full message header (type, msg_len, msg, rem_len, rem)
                        current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length() + LEN_FIELD_LEN + nickname.length();
                        sprintf(buffer + current_offset, "%05d", current_payload_len); // len
                        current_offset += LEN_FIELD_LEN;
                        buffer[current_offset++] = 'B'; // tipo
                        sprintf(buffer + current_offset, "%05d", (int)trozo.length()); // len_msg
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // msg
                        current_offset += trozo.length();
                        sprintf(buffer + current_offset, "%05d", (int)nickname.length()); // len_rem
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, nickname.c_str(), nickname.length()); // rem
                    } else { // Subsequent fragments only need type, msg_len, msg
                        current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length();
                        sprintf(buffer + current_offset, "%05d", current_payload_len); // len
                        current_offset += LEN_FIELD_LEN;
                        buffer[current_offset++] = 'B'; // tipo
                        sprintf(buffer + current_offset, "%05d", (int)trozo.length()); // len_msg
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // msg
                    }

                    write(sock, buffer, 500);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Enviando fragmento " << seq << " de " << totalPkg << " (Mensaje Broadcast)..." << std::endl;
                    seq++;
                }
                break;
            }
            case '4': { // Enviar archivo 'F'
                std::cout << "Destinatario del archivo: ";
                std::getline(std::cin, destino);
                std::cout << "Ruta del archivo a enviar: ";
                std::string filePath;
                std::getline(std::cin, filePath);

                std::ifstream file(filePath, std::ios::binary | std::ios::ate);
                if (!file.is_open()) {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cerr << "Error: No se pudo abrir el archivo." << std::endl;
                    break;
                }

                long fileSize = file.tellg();
                file.seekg(0, std::ios::beg);
                std::vector<char> fileBuffer(fileSize);
                file.read(fileBuffer.data(), fileSize);
                file.close();

                int hash = calcularHash(fileBuffer.data(), fileSize);

                std::string nombreArchivo = filePath.substr(filePath.find_last_of('/') + 1);

                // Calculate max payload size for file content after all headers
                // First fragment overhead: Type(1) + len_dest(5) + dest(N) + len_filename(100) + filename(M) + len_chunk(18) + hash(5)
                // Subsequent fragment overhead: Type(1) + len_chunk(18) + hash(5)
                const int FILE_NAME_LEN_FIELD = 100; // Fixed size for filename length field
                const int FILE_CHUNK_LEN_FIELD = 18; // Fixed size for chunk length field
                const int HASH_FIELD_LEN = 5;

                int first_fragment_data_capacity = 500 - PROTOCOL_HEADER_LEN - (LEN_FIELD_LEN + destino.length() + FILE_NAME_LEN_FIELD + nombreArchivo.length() + FILE_CHUNK_LEN_FIELD + HASH_FIELD_LEN);
                int subsequent_fragment_data_capacity = 500 - PROTOCOL_HEADER_LEN - (FILE_CHUNK_LEN_FIELD + HASH_FIELD_LEN);

                std::vector<std::string> file_pieces;
                if (fileSize > 0) {
                    // First chunk
                    long current_pos = 0;
                    long first_chunk_size = std::min((long)first_fragment_data_capacity, fileSize);
                    file_pieces.push_back(std::string(fileBuffer.begin() + current_pos, fileBuffer.begin() + current_pos + first_chunk_size));
                    current_pos += first_chunk_size;

                    // Subsequent chunks
                    while (current_pos < fileSize) {
                        long chunk_size = std::min((long)subsequent_fragment_data_capacity, fileSize - current_pos);
                        file_pieces.push_back(std::string(fileBuffer.begin() + current_pos, fileBuffer.begin() + current_pos + chunk_size));
                        current_pos += chunk_size;
                    }
                } else {
                    file_pieces.push_back(""); // For empty files, send one fragment with empty data
                }

                int totalPkg = file_pieces.size();
                if (totalPkg == 0) totalPkg = 1; // Ensure at least one package for empty file

                int seq = 1;

                for (const auto& trozo : file_pieces) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // seq, tot

                    int current_payload_len;
                    int current_offset = FRAGMENTATION_HEADER_LEN;

                    if (seq == 1) {
                        current_payload_len = TYPE_LEN + LEN_FIELD_LEN + destino.length() + FILE_NAME_LEN_FIELD + nombreArchivo.length() + FILE_CHUNK_LEN_FIELD + trozo.length() + HASH_FIELD_LEN;
                        sprintf(buffer + current_offset, "%05d", current_payload_len); // len
                        current_offset += LEN_FIELD_LEN;
                        buffer[current_offset++] = 'F'; // type 'F'
                        sprintf(buffer + current_offset, "%05d", (int)destino.length()); // len_dest
                        current_offset += LEN_FIELD_LEN;
                        memcpy(buffer + current_offset, destino.c_str(), destino.length()); // dest
                        current_offset += destino.length();
                        sprintf(buffer + current_offset, "%0100d", (int)nombreArchivo.length()); // len_filename (100-padded)
                        current_offset += FILE_NAME_LEN_FIELD;
                        memcpy(buffer + current_offset, nombreArchivo.c_str(), nombreArchivo.length()); // filename
                        current_offset += nombreArchivo.length();
                        sprintf(buffer + current_offset, "%018ld", trozo.length()); // len_chunk (18-padded)
                        current_offset += FILE_CHUNK_LEN_FIELD;
                        memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // file_chunk
                        current_offset += trozo.length();
                        sprintf(buffer + current_offset, "%05d", hash); // hash
                    } else {
                        current_payload_len = TYPE_LEN + FILE_CHUNK_LEN_FIELD + trozo.length() + HASH_FIELD_LEN;
                        sprintf(buffer + current_offset, "%05d", current_payload_len); // len
                        current_offset += LEN_FIELD_LEN;
                        buffer[current_offset++] = 'F'; // type 'F'
                        sprintf(buffer + current_offset, "%018ld", trozo.length()); // len_chunk (18-padded)
                        current_offset += FILE_CHUNK_LEN_FIELD;
                        memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // file_chunk
                        current_offset += trozo.length();
                        sprintf(buffer + current_offset, "%05d", hash); // hash
                    }

                    write(sock, buffer, 500);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Enviando fragmento " << seq << " de " << totalPkg << " (Archivo)..." << std::endl;
                    seq++;
                }
                std::lock_guard<std::mutex> lock(coutMux);
                std::cout << "Archivo '" << nombreArchivo << "' enviado a " << destino << ". Esperando confirmacion..." << std::endl;
                break;
            }
            case '5': { // Unirse a partida 'J'
                if (!enPartida) {
                    memset(buffer, '#', 500);
                    sprintf(buffer, "%05d%05d", 1, 1); // seq, tot
                    int offset = FRAGMENTATION_HEADER_LEN;
                    sprintf(buffer + offset, "%05d", 1); // len: 1 for 'J'
                    offset += LEN_FIELD_LEN;
                    buffer[offset++] = 'J'; // tipo
                    write(sock, buffer, 500);
                    enPartida = true;
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Ya estas en partida." << std::endl;
                }
                break;
            }
            case '6': { // Realizar jugada 'P'
                if (enPartida && tuTurno) {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "Ingresa la posicion (1-9): ";
                    char pos;
                    std::cin >> pos;
                    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                    if (pos >= '1' && pos <= '9') {
                        memset(buffer, '#', 500);
                        sprintf(buffer, "%05d%05d", 1, 1); // seq, tot
                        int offset = FRAGMENTATION_HEADER_LEN;
                        sprintf(buffer + offset, "%05d", 3); // len: 3 for 'P' + pos + simbolo
                        offset += LEN_FIELD_LEN;
                        buffer[offset++] = 'P'; // tipo
                        buffer[offset++] = pos; // posicion
                        buffer[offset++] = simbolo; // simbolo del jugador
                        write(sock, buffer, 500);
                        tuTurno = false; // Ya jugaste
                    } else {
                        std::cout << "Posicion invalida. Debe ser un numero del 1 al 9." << std::endl;
                    }
                } else if (enPartida && !tuTurno) {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "No es tu turno o no estas en una partida activa." << std::endl;
                } else {
                    std::lock_guard<std::mutex> lock(coutMux);
                    std::cout << "No estas en una partida activa." << std::endl;
                }
                break;
            }
            case '7': { // Salir 'Q'
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", 1, 1); // seq, tot
                int offset = FRAGMENTATION_HEADER_LEN;
                sprintf(buffer + offset, "%05d", 1); // len: 1 for 'Q'
                offset += LEN_FIELD_LEN;
                buffer[offset++] = 'Q'; // tipo
                write(sock, buffer, 500);
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Dar tiempo para que el servidor procese
                close(sock);
                return;
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

    // Create directory for received files if it doesn't exist
    if (system("mkdir -p archivos_recibidos") == -1) {
        perror("mkdir");
        // Not critical, continue without error
    }

    std::thread(recibirMensajes, sock).detach();
    mostrarMenu(sock);

    return 0;
}
