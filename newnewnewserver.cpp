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

// --- Protocol Constants ---
const int SEQ_LEN = 5;
const int TOT_LEN = 5;
const int LEN_FIELD_LEN = 5; // Length of the length field itself
const int TYPE_LEN = 1;
const int FRAGMENTATION_HEADER_LEN = SEQ_LEN + TOT_LEN; // 10 bytes for seq and tot
const int PROTOCOL_HEADER_LEN = FRAGMENTATION_HEADER_LEN + LEN_FIELD_LEN + TYPE_LEN; // 10 + 5 + 1 = 16 bytes

std::vector<std::string> partir(const std::string& msg, int tamMax) {
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
    
    // Total size of payload: result char + message length
    int totalSizePayload = TYPE_LEN + TYPE_LEN + mensaje.length(); // 'A' + 'S'/'F' + message

    // Format: [seq(5)][tot(5)][len(5)]'A'[result(1)][message]
    sprintf(buffer, "%05d%05d", 1, 1); // seq, tot

    int offset = FRAGMENTATION_HEADER_LEN;
    sprintf(buffer + offset, "%05d", totalSizePayload); // len
    offset += LEN_FIELD_LEN;
    buffer[offset++] = 'A'; // Type 'A' for ACK
    buffer[offset++] = resultado; // 'S' for success, 'F' for failure
    memcpy(buffer + offset, mensaje.c_str(), mensaje.length());

    write(sock, buffer, 500);
    this_thread::sleep_for(chrono::milliseconds(50));
}

void procesarMensaje(int sock, const string &nick, const vector<string> &pkgs, char tipo) {
    char buffer[500];

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

        // Calculate max payload size for list of users
        // Payload: Type(1) + len_users(5) + users(N)
        int max_data_payload_per_fragment = 500 - PROTOCOL_HEADER_LEN;
        int overhead_per_fragment = LEN_FIELD_LEN; // For len_users for the chunk in subsequent fragments

        vector<string> piezas;
        if (mensaje.length() + LEN_FIELD_LEN <= max_data_payload_per_fragment) {
            piezas.push_back(mensaje); // No fragmentation needed
        } else {
            int first_chunk_size = max_data_payload_per_fragment - LEN_FIELD_LEN;
            piezas.push_back(mensaje.substr(0, first_chunk_size));
            string remaining_msg = mensaje.substr(first_chunk_size);
            int max_sub_chunk_size = max_data_payload_per_fragment - LEN_FIELD_LEN;
            vector<string> sub_pieces = partir(remaining_msg, max_sub_chunk_size);
            piezas.insert(piezas.end(), sub_pieces.begin(), sub_pieces.end());
        }


        int totalPkg = piezas.size();
        int seq = 1;
        for (auto &trozo : piezas) {
            memset(buffer, '#', 500);
            sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // seq, tot
            
            int current_payload_len;
            int current_offset = FRAGMENTATION_HEADER_LEN;

            // 'l' type for list of users.
            current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length();
            sprintf(buffer + current_offset, "%05d", current_payload_len); // len
            current_offset += LEN_FIELD_LEN;
            buffer[current_offset++] = 'l'; // Type
            sprintf(buffer + current_offset, "%05d", (int)trozo.length()); // len of message content
            current_offset += LEN_FIELD_LEN;
            memcpy(buffer + current_offset, trozo.c_str(), trozo.length()); // message content
            
            write(sock, buffer, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
            seq++;
        }
    }
    else if (tipo == 'M') {
        // Original sender's message. Reassemble if fragmented.
        // First fragment: [seq(5)][tot(5)][len_total_payload(5)]'M'[len_msg(5)][msg_chunk(N)][len_dest(5)][dest(M)]
        // Subsequent: [seq(5)][tot(5)][len_total_payload(5)]'M'[len_msg(5)][msg_chunk(P)]

        string mensaje;
        string destino_str;
        
        // Reconstruct the full message content and extract destination
        for (size_t i = 0; i < pkgs.size(); ++i) {
            const string& current_pkg = pkgs[i];
            int current_pkg_data_offset = PROTOCOL_HEADER_LEN; // After seq, tot, len, type

            int len_msg_chunk = std::stoi(current_pkg.substr(current_pkg_data_offset, LEN_FIELD_LEN));
            string msg_chunk = current_pkg.substr(current_pkg_data_offset + LEN_FIELD_LEN, len_msg_chunk);
            mensaje += msg_chunk;

            if (i == 0) { // Only first fragment contains destination
                int len_dest_offset = current_pkg_data_offset + LEN_FIELD_LEN + len_msg_chunk;
                int len_dest = std::stoi(current_pkg.substr(len_dest_offset, LEN_FIELD_LEN));
                destino_str = current_pkg.substr(len_dest_offset + LEN_FIELD_LEN, len_dest);
            }
        }
        
        lock_guard<mutex> lock(mapaMutex);
        if (mapaSockets.find(destino_str) != mapaSockets.end()) {
            int destino_sock = mapaSockets[destino_str];
            
            // Now, prepare to re-send to the recipient, applying server's fragmentation logic
            // Max payload for 'm' message: Type(1) + len_msg(5) + msg + len_rem(5) + rem
            int max_data_payload_per_fragment = 500 - FRAGMENTATION_HEADER_LEN - LEN_FIELD_LEN; // Available space after seq, tot, len_total_payload
            int initial_msg_overhead = TYPE_LEN + LEN_FIELD_LEN + LEN_FIELD_LEN + nick.length(); // 'm' + len_msg + len_rem + rem
            int subsequent_msg_overhead = TYPE_LEN + LEN_FIELD_LEN; // 'm' + len_msg

            vector<string> piezas_reenvio;
            if (mensaje.length() + initial_msg_overhead <= max_data_payload_per_fragment) {
                piezas_reenvio.push_back(mensaje);
            } else {
                int first_chunk_size = max_data_payload_per_fragment - initial_msg_overhead;
                piezas_reenvio.push_back(mensaje.substr(0, first_chunk_size));
                string remaining_msg = mensaje.substr(first_chunk_size);
                int max_sub_chunk_size = max_data_payload_per_fragment - subsequent_msg_overhead;
                vector<string> sub_pieces = partir(remaining_msg, max_sub_chunk_size);
                piezas_reenvio.insert(piezas_reenvio.end(), sub_pieces.begin(), sub_pieces.end());
            }

            int totalPkgReenvio = piezas_reenvio.size();
            int seqReenvio = 1;

            for (const auto& trozo : piezas_reenvio) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", (seqReenvio == totalPkgReenvio ? 0 : seqReenvio), totalPkgReenvio);

                int current_payload_len;
                int current_offset = FRAGMENTATION_HEADER_LEN;

                if (seqReenvio == 1) { // First fragment includes sender info
                    current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length() + LEN_FIELD_LEN + nick.length();
                    sprintf(buffer + current_offset, "%05d", current_payload_len);
                    current_offset += LEN_FIELD_LEN;
                    buffer[current_offset++] = 'm'; // Type
                    sprintf(buffer + current_offset, "%05d", (int)trozo.length());
                    current_offset += LEN_FIELD_LEN;
                    memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
                    current_offset += trozo.length();
                    sprintf(buffer + current_offset, "%05d", (int)nick.length());
                    current_offset += LEN_FIELD_LEN;
                    memcpy(buffer + current_offset, nick.c_str(), nick.length());
                } else { // Subsequent fragments only contain message content
                    current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length();
                    sprintf(buffer + current_offset, "%05d", current_payload_len);
                    current_offset += LEN_FIELD_LEN;
                    buffer[current_offset++] = 'm'; // Type
                    sprintf(buffer + current_offset, "%05d", (int)trozo.length());
                    current_offset += LEN_FIELD_LEN;
                    memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
                }

                write(destino_sock, buffer, 500);
                this_thread::sleep_for(chrono::milliseconds(50));
                seqReenvio++;
            }
        } else {
            enviarM(sock, "El usuario '" + destino_str + "' no esta conectado.");
        }
    }
    else if (tipo == 'B') {
        // Broadcast message reassembly
        // First fragment: [seq(5)][tot(5)][len_total_payload(5)]'B'[len_msg(5)][msg_chunk(N)][len_rem(5)][rem(M)]
        // Subsequent: [seq(5)][tot(5)][len_total_payload(5)]'B'[len_msg(5)][msg_chunk(P)]
        string mensaje;
        for (size_t i = 0; i < pkgs.size(); ++i) {
            const string& current_pkg = pkgs[i];
            int current_pkg_data_offset = PROTOCOL_HEADER_LEN;
            int len_msg_chunk = std::stoi(current_pkg.substr(current_pkg_data_offset, LEN_FIELD_LEN));
            string msg_chunk = current_pkg.substr(current_pkg_data_offset + LEN_FIELD_LEN, len_msg_chunk);
            mensaje += msg_chunk;
        }

        // Prepare to broadcast, applying server's fragmentation logic
        int max_data_payload_per_fragment = 500 - FRAGMENTATION_HEADER_LEN - LEN_FIELD_LEN;
        int initial_msg_overhead = TYPE_LEN + LEN_FIELD_LEN + LEN_FIELD_LEN + nick.length(); // 'b' + len_msg + len_rem + rem
        int subsequent_msg_overhead = TYPE_LEN + LEN_FIELD_LEN; // 'b' + len_msg

        vector<string> piezas_reenvio;
        if (mensaje.length() + initial_msg_overhead <= max_data_payload_per_fragment) {
            piezas_reenvio.push_back(mensaje);
        } else {
            int first_chunk_size = max_data_payload_per_fragment - initial_msg_overhead;
            piezas_reenvio.push_back(mensaje.substr(0, first_chunk_size));
            string remaining_msg = mensaje.substr(first_chunk_size);
            int max_sub_chunk_size = max_data_payload_per_fragment - subsequent_msg_overhead;
            vector<string> sub_pieces = partir(remaining_msg, max_sub_chunk_size);
            piezas_reenvio.insert(piezas_reenvio.end(), sub_pieces.begin(), sub_pieces.end());
        }

        int totalPkgReenvio = piezas_reenvio.size();
        int seqReenvio = 1;

        for (const auto& trozo : piezas_reenvio) {
            memset(buffer, '#', 500);
            sprintf(buffer, "%05d%05d", (seqReenvio == totalPkgReenvio ? 0 : seqReenvio), totalPkgReenvio);

            int current_payload_len;
            int current_offset = FRAGMENTATION_HEADER_LEN;

            if (seqReenvio == 1) { // First fragment includes sender info
                current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length() + LEN_FIELD_LEN + nick.length();
                sprintf(buffer + current_offset, "%05d", current_payload_len);
                current_offset += LEN_FIELD_LEN;
                buffer[current_offset++] = 'b'; // Type
                sprintf(buffer + current_offset, "%05d", (int)trozo.length());
                current_offset += LEN_FIELD_LEN;
                memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
                current_offset += trozo.length();
                sprintf(buffer + current_offset, "%05d", (int)nick.length());
                current_offset += LEN_FIELD_LEN;
                memcpy(buffer + current_offset, nick.c_str(), nick.length());
            } else { // Subsequent fragments only contain message content
                current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length();
                sprintf(buffer + current_offset, "%05d", current_payload_len);
                current_offset += LEN_FIELD_LEN;
                buffer[current_offset++] = 'b'; // Type
                sprintf(buffer + current_offset, "%05d", (int)trozo.length());
                current_offset += LEN_FIELD_LEN;
                memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
            }
            
            lock_guard<mutex> lock(mapaMutex);
            for (auto &par : mapaSockets) {
                if (par.first != nick) { // Do not send back to sender
                    write(par.second, buffer, 500);
                    this_thread::sleep_for(chrono::milliseconds(50));
                }
            }
            seqReenvio++;
        }
    }
    else if (tipo == 'F') { // Client wants to send a file TO another client. Server acts as a router.
        // First fragment: [seq(5)][tot(5)][len_total_payload(5)]'F'[len_dest(5)][dest(N)][len_filename(100)][filename(M)][len_file_chunk(18)][file_chunk(X)][hash(5)]
        // Subsequent fragments: [seq(5)][tot(5)][len_total_payload(5)]'F'[len_file_chunk(18)][file_chunk(Y)][hash(5)]
        
        string file_content_received;
        string destino_file_transfer;
        string filename_received;
        int hash_received = -1;

        const int FILE_NAME_LEN_FIELD = 100;
        const int FILE_CHUNK_LEN_FIELD = 18;
        const int HASH_FIELD_LEN = 5;

        for (size_t i = 0; i < pkgs.size(); ++i) {
            const string& current_pkg = pkgs[i];
            int current_pkg_data_offset = PROTOCOL_HEADER_LEN; // After seq, tot, len, type

            if (i == 0) {
                // First fragment contains destination, filename, and first chunk
                int len_dest = std::stoi(current_pkg.substr(current_pkg_data_offset, LEN_FIELD_LEN));
                destino_file_transfer = current_pkg.substr(current_pkg_data_offset + LEN_FIELD_LEN, len_dest);
                current_pkg_data_offset += LEN_FIELD_LEN + len_dest;

                int len_filename = std::stoi(current_pkg.substr(current_pkg_data_offset, FILE_NAME_LEN_FIELD));
                filename_received = current_pkg.substr(current_pkg_data_offset + FILE_NAME_LEN_FIELD, len_filename);
                current_pkg_data_offset += FILE_NAME_LEN_FIELD + len_filename;
                
                long len_chunk = std::stol(current_pkg.substr(current_pkg_data_offset, FILE_CHUNK_LEN_FIELD));
                file_content_received += current_pkg.substr(current_pkg_data_offset + FILE_CHUNK_LEN_FIELD, len_chunk);
                current_pkg_data_offset += FILE_CHUNK_LEN_FIELD + len_chunk;

                hash_received = std::stoi(current_pkg.substr(current_pkg_data_offset, HASH_FIELD_LEN));
            } else {
                // Subsequent fragments only contain file chunk and hash
                long len_chunk = std::stol(current_pkg.substr(current_pkg_data_offset, FILE_CHUNK_LEN_FIELD));
                file_content_received += current_pkg.substr(current_pkg_data_offset + FILE_CHUNK_LEN_FIELD, len_chunk);
                
                hash_received = std::stoi(current_pkg.substr(current_pkg_data_offset + FILE_CHUNK_LEN_FIELD + len_chunk, HASH_FIELD_LEN));
            }
        }
        
        lock_guard<mutex> lock(mapaMutex); // Corrected: use mapaMutex
        if (mapaSockets.find(destino_file_transfer) != mapaSockets.end()) {
            cout << "Servidor: Reenviando archivo '" << filename_received << "' de '" << nick << "' a '" << destino_file_transfer << "'." << endl;

            int destino_sock = mapaSockets[destino_file_transfer];

            // Re-fragment the file content for re-sending
            // Need to account for all headers for each fragment
            int first_fragment_data_capacity = 500 - PROTOCOL_HEADER_LEN - (LEN_FIELD_LEN + nick.length() + FILE_NAME_LEN_FIELD + filename_received.length() + FILE_CHUNK_LEN_FIELD + HASH_FIELD_LEN);
            int subsequent_fragment_data_capacity = 500 - PROTOCOL_HEADER_LEN - (FILE_CHUNK_LEN_FIELD + HASH_FIELD_LEN);

            std::vector<std::string> pieces_for_forwarding;
            if (file_content_received.length() > 0) {
                // First chunk
                long current_pos = 0;
                long first_chunk_size = std::min((long)first_fragment_data_capacity, (long)file_content_received.length());
                pieces_for_forwarding.push_back(file_content_received.substr(current_pos, first_chunk_size));
                current_pos += first_chunk_size;

                // Subsequent chunks
                while (current_pos < file_content_received.length()) {
                    long chunk_size = std::min((long)subsequent_fragment_data_capacity, (long)file_content_received.length() - current_pos);
                    pieces_for_forwarding.push_back(file_content_received.substr(current_pos, chunk_size));
                    current_pos += chunk_size;
                }
            } else {
                pieces_for_forwarding.push_back(""); // For empty files
            }
            
            int totalPkgReenvio = pieces_for_forwarding.size();
            if (totalPkgReenvio == 0) totalPkgReenvio = 1; // Ensure at least one package for empty file

            int seqReenvio = 1;
            bool reenvioExitoso = true;

            for (const auto& trozo : pieces_for_forwarding) {
                memset(buffer, '#', 500);
                sprintf(buffer, "%05d%05d", (seqReenvio == totalPkgReenvio ? 0 : seqReenvio), totalPkgReenvio);
                
                int current_payload_len;
                int current_offset = FRAGMENTATION_HEADER_LEN;
                
                if (seqReenvio == 1) { // First fragment includes sender and filename info
                    current_payload_len = TYPE_LEN + LEN_FIELD_LEN + nick.length() + FILE_NAME_LEN_FIELD + filename_received.length() + FILE_CHUNK_LEN_FIELD + trozo.length() + HASH_FIELD_LEN;
                    sprintf(buffer + current_offset, "%05d", current_payload_len);
                    current_offset += LEN_FIELD_LEN;
                    buffer[current_offset++] = 'f'; // Type 'f' for file
                    sprintf(buffer + current_offset, "%05d", (int)nick.length());
                    current_offset += LEN_FIELD_LEN;
                    memcpy(buffer + current_offset, nick.c_str(), nick.length());
                    current_offset += nick.length();
                    sprintf(buffer + current_offset, "%0100d", (int)filename_received.length());
                    current_offset += FILE_NAME_LEN_FIELD;
                    memcpy(buffer + current_offset, filename_received.c_str(), filename_received.length());
                    current_offset += filename_received.length();
                    sprintf(buffer + current_offset, "%018ld", trozo.length());
                    current_offset += FILE_CHUNK_LEN_FIELD;
                    memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
                    current_offset += trozo.length();
                    sprintf(buffer + current_offset, "%05d", hash_received);
                } else { // Subsequent fragments only contain file chunk and hash
                    current_payload_len = TYPE_LEN + FILE_CHUNK_LEN_FIELD + trozo.length() + HASH_FIELD_LEN;
                    sprintf(buffer + current_offset, "%05d", current_payload_len);
                    current_offset += LEN_FIELD_LEN;
                    buffer[current_offset++] = 'f'; // Type 'f' for file
                    sprintf(buffer + current_offset, "%018ld", trozo.length());
                    current_offset += FILE_CHUNK_LEN_FIELD;
                    memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
                    current_offset += trozo.length();
                    sprintf(buffer + current_offset, "%05d", hash_received);
                }
                
                if (write(destino_sock, buffer, 500) < 0) {
                    perror("write error during file re-forwarding");
                    reenvioExitoso = false;
                    break;
                }
                this_thread::sleep_for(chrono::milliseconds(50));
                seqReenvio++;
            }

            if (reenvioExitoso) {
                cout << "Servidor: Archivo '" << filename_received << "' reenviado exitosamente a '" << destino_file_transfer << "'." << endl;
                enviarACKArchivo(sock, 'S', "Archivo '" + filename_received + "' enviado exitosamente a '" + destino_file_transfer + "'.");
            } else {
                cerr << "Servidor: Fallo al reenviar archivo '" << filename_received << "' a '" << destino_file_transfer << "'." << endl;
                enviarACKArchivo(sock, 'F', "Fallo al enviar archivo '" + filename_received + "' a '" + destino_file_transfer + "'.");
            }

        } else {
            cerr << "Servidor: Destino '" << destino_file_transfer << "' no encontrado para reenviar archivo." << endl;
            enviarACKArchivo(sock, 'F', "Error: Destino '" + destino_file_transfer + "' no encontrado para el archivo.");
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
            enviarM(sock, "Esperando por otro jugador para iniciar la partida...");
        }
        else if (!partida.activa && !jugadorEnEspera.empty() && jugadorEnEspera != nick) {
            partida.activa = true;
            partida.jugadorO = jugadorEnEspera;
            partida.jugadorX = nick;
            jugadorEnEspera = "";

            enviarM(mapaSockets[partida.jugadorO], "La partida ha iniciado! Eres 'O'.");
            enviarM(mapaSockets[partida.jugadorX], "La partida ha iniciado! Eres 'X'.");
            enviarX_aTodos(); // Send initial board state
            enviarT(partida.jugadorO, 'O'); // Set turn for 'O'
        }
        else if (partida.activa) {
            enviarM(sock, "Ya hay una partida activa. Quieres unirte como espectador? (Usa la opcion 'V')");
        } else { // jugadorEnEspera == nick
             enviarM(sock, "Ya estas esperando por otro jugador.");
        }
    }
    else if (tipo == 'V') { // Client wants to view Tic-Tac-Toe game
        lock_guard<mutex> lock(partidaMutex);
        if (partida.activa) {
            // Check if already a player or spectator
            if (nick == partida.jugadorO || nick == partida.jugadorX || 
                std::find(partida.espectadores.begin(), partida.espectadores.end(), nick) != partida.espectadores.end()) {
                enviarM(sock, "Ya eres parte de la partida (jugador o espectador).");
                return;
            }
            partida.espectadores.push_back(nick);
            char pkt[500];
            memset(pkt, '#', 500);
            sprintf(pkt, "%05d%05d", 1, 1); // seq, tot
            int offset = FRAGMENTATION_HEADER_LEN;
            sprintf(pkt + offset, "%05d", TYPE_LEN + 9); // len: 1 for 'X' + 9 for board
            offset += LEN_FIELD_LEN;
            pkt[offset++] = 'X'; // Type 'X' for board state
            memcpy(pkt + offset, partida.tablero, 9); // Board data
            write(sock, pkt, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
            enviarM(sock, "Te has unido como espectador.");
        } else {
            enviarM(sock, "La partida no esta activa para ver.");
        }
    }
    else if (tipo == 'P') { // Player makes a move in Tic-Tac-Toe
        // P[pos(1)][simbolo(1)]
        int data_offset = PROTOCOL_HEADER_LEN; // After seq, tot, len, type
        char pos = pkgs[0][data_offset]; 
        char simb = pkgs[0][data_offset + 1];

        lock_guard<mutex> lock(partidaMutex);
        if (!((nick == partida.jugadorO && simb == 'O' && partida.turno == 'O') ||
              (nick == partida.jugadorX && simb == 'X' && partida.turno == 'X'))) {
            char errPkt[500];
            memset(errPkt, '#', 500);
            sprintf(errPkt, "%05d%05d", 1, 1); // seq, tot
            int offset = FRAGMENTATION_HEADER_LEN;
            sprintf(errPkt + offset, "%05d", TYPE_LEN + TYPE_LEN + LEN_FIELD_LEN + 14); // len: 'E' + '7' + len_msg + "No es tu turno"
            offset += LEN_FIELD_LEN;
            errPkt[offset++] = 'E'; // Error type
            errPkt[offset++] = '7'; // Error code: Not your turn
            sprintf(errPkt + offset, "%05d", 14); // len of error message
            offset += LEN_FIELD_LEN;
            memcpy(errPkt + offset, "No es tu turno", 14);
            write(sock, errPkt, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
            return;
        }

        int idx = pos - '1';
        if (idx < 0 || idx > 8 || partida.tablero[idx] != '_') {
            char errPkt[500];
            memset(errPkt, '#', 500);
            sprintf(errPkt, "%05d%05d", 1, 1); // seq, tot
            int offset = FRAGMENTATION_HEADER_LEN;
            sprintf(errPkt + offset, "%05d", TYPE_LEN + TYPE_LEN + LEN_FIELD_LEN + 16); // len: 'E' + '6' + len_msg + "Posicion ocupada"
            offset += LEN_FIELD_LEN;
            errPkt[offset++] = 'E'; // Error type
            errPkt[offset++] = '6'; // Error code: Position occupied/invalid
            sprintf(errPkt + offset, "%05d", 16); // len of error message
            offset += LEN_FIELD_LEN;
            memcpy(errPkt + offset, "Posicion ocupada", 16);
            write(sock, errPkt, 500);
            this_thread::sleep_for(chrono::milliseconds(50));
            return;
        }

        partida.tablero[idx] = simb;

        if (ganador(simb)) {
            enviarX_aTodos();
            this_thread::sleep_for(chrono::milliseconds(50));
            finalizarPartida(simb); // 'W' for winner
        }
        else if (tableroLleno()) {
            enviarX_aTodos();
            finalizarPartida('E'); // 'E' for empate (draw)
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
    
    // Calculate max payload size for message content + sender nickname + their lengths
    int max_data_payload_per_fragment = 500 - FRAGMENTATION_HEADER_LEN - LEN_FIELD_LEN; // Available space after seq, tot, len_total_payload
    int initial_msg_overhead = TYPE_LEN + LEN_FIELD_LEN + LEN_FIELD_LEN + lenRem; // 'm' + len_msg + len_rem + rem
    int subsequent_msg_overhead = TYPE_LEN + LEN_FIELD_LEN; // 'm' + len_msg

    vector<string> piezas;
    if (lenMsg + initial_msg_overhead <= max_data_payload_per_fragment) {
        piezas.push_back(msg);
    } else {
        int first_chunk_size = max_data_payload_per_fragment - initial_msg_overhead;
        piezas.push_back(msg.substr(0, first_chunk_size));
        string remaining_msg = msg.substr(first_chunk_size);
        int max_sub_chunk_size = max_data_payload_per_fragment - subsequent_msg_overhead;
        vector<string> sub_pieces = partir(remaining_msg, max_sub_chunk_size);
        piezas.insert(piezas.end(), sub_pieces.begin(), sub_pieces.end());
    }

    int totalPkg = piezas.size();
    int seq = 1;

    char buffer[500];
    for (const auto& trozo : piezas) {
        memset(buffer, '#', 500);
        sprintf(buffer, "%05d%05d", (seq == totalPkg ? 0 : seq), totalPkg); // seq, tot

        int current_payload_len;
        int current_offset = FRAGMENTATION_HEADER_LEN;

        if (seq == 1) { // First fragment includes sender info
            current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length() + LEN_FIELD_LEN + lenRem;
            sprintf(buffer + current_offset, "%05d", current_payload_len);
            current_offset += LEN_FIELD_LEN;
            buffer[current_offset++] = 'm'; // Type
            sprintf(buffer + current_offset, "%05d", (int)trozo.length());
            current_offset += LEN_FIELD_LEN;
            memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
            current_offset += trozo.length();
            sprintf(buffer + current_offset, "%05d", lenRem);
            current_offset += LEN_FIELD_LEN;
            memcpy(buffer + current_offset, remitente.c_str(), lenRem);
        } else { // Subsequent fragments only contain message content
            current_payload_len = TYPE_LEN + LEN_FIELD_LEN + trozo.length();
            sprintf(buffer + current_offset, "%05d", current_payload_len);
            current_offset += LEN_FIELD_LEN;
            buffer[current_offset++] = 'm'; // Type
            sprintf(buffer + current_offset, "%05d", (int)trozo.length());
            current_offset += LEN_FIELD_LEN;
            memcpy(buffer + current_offset, trozo.c_str(), trozo.length());
        }

        write(sock, buffer, 500);
        this_thread::sleep_for(chrono::milliseconds(50));
        seq++;
    }
}

void enviarX_aTodos() {
    char pkt[500];
    memset(pkt, '#', 500);
    sprintf(pkt, "%05d%05d", 1, 1); // seq, tot
    int offset = FRAGMENTATION_HEADER_LEN;
    sprintf(pkt + offset, "%05d", TYPE_LEN + 9); // len: Type 'X' + 9 board chars
    offset += LEN_FIELD_LEN;
    pkt[offset++] = 'X'; // Type 'X' for board state
    memcpy(pkt + offset, partida.tablero, 9); // Board data

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
    sprintf(pkt, "%05d%05d", 1, 1); // seq, tot
    int offset = FRAGMENTATION_HEADER_LEN;
    sprintf(pkt + offset, "%05d", TYPE_LEN + TYPE_LEN); // len: Type 'T' + 1 char for symbol
    offset += LEN_FIELD_LEN;
    pkt[offset++] = 'T'; // Type 'T' for Turn
    pkt[offset++] = simbolo; // Symbol 'O' or 'X'
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
    
    // Packet for Player O
    memset(pktO, '#', 500);
    sprintf(pktO, "%05d%05d", 1, 1); // seq, tot
    int offsetO = FRAGMENTATION_HEADER_LEN;
    sprintf(pktO + offsetO, "%05d", TYPE_LEN + TYPE_LEN); // len: Type 'O' + 1 char for result
    offsetO += LEN_FIELD_LEN;
    pktO[offsetO++] = 'O'; // Type 'O' for Game Over
    pktO[offsetO++] = (resultado == 'O') ? 'W' : (resultado == 'X') ? 'L' : 'E'; // 'W'in, 'L'oss, 'E'mpate
    
    // Packet for Player X
    memset(pktX, '#', 500);
    sprintf(pktX, "%05d%05d", 1, 1); // seq, tot
    int offsetX = FRAGMENTATION_HEADER_LEN;
    sprintf(pktX + offsetX, "%05d", TYPE_LEN + TYPE_LEN); // len: Type 'O' + 1 char for result
    offsetX += LEN_FIELD_LEN;
    pktX[offsetX++] = 'O'; // Type 'O' for Game Over
    pktX[offsetX++] = (resultado == 'X') ? 'W' : (resultado == 'O') ? 'L' : 'E'; // 'W'in, 'L'oss, 'E'mpate
    
    // Packet for Spectators
    memset(pktE, '#', 500);
    sprintf(pktE, "%05d%05d", 1, 1); // seq, tot
    int offsetE = FRAGMENTATION_HEADER_LEN;
    sprintf(pktE + offsetE, "%05d", TYPE_LEN + TYPE_LEN); // len: Type 'O' + 1 char for result
    offsetE += LEN_FIELD_LEN;
    pktE[offsetE++] = 'O'; // Type 'O' for Game Over
    pktE[offsetE++] = 'E'; // Always 'E' for spectators (Empate/End)

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

    partida = Partida(); // Reset game state
    
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

        if (pkg.length() < FRAGMENTATION_HEADER_LEN) {
            cerr << "Error: Paquete demasiado corto para cabecera de fragmentacion del cliente " << nick << endl;
            continue;
        }

        int seq = stoi(pkg.substr(0, SEQ_LEN)); // Now 5 digits
        int tot = stoi(pkg.substr(SEQ_LEN, TOT_LEN)); // Now 5 digits

        if (seq < 0 || seq > tot || tot <= 0) {
            cerr << "Error de fragmento: Valores de secuencia/total invalidos (seq=" << seq << ", tot=" << tot << ") del cliente " << nick << ". Reiniciando reensamblaje." << endl;
            vc.clear();
            expected_total_pkgs = 0;
            continue;
        }

        int idx = (seq == 0) ? tot - 1 : seq - 1; // 0 means the last package, its index is tot-1

        if (expected_total_pkgs == 0) {
            expected_total_pkgs = tot;
            vc.resize(tot);
            current_message_number++; // New message starting
            cout << "Recibiendo mensaje #" << current_message_number << " del cliente " << nick << endl;
        } else if (tot != expected_total_pkgs) {
            cerr << "Error de fragmento: El numero total de paquetes cambio (esperado "
                 << expected_total_pkgs << ", recibido " << tot << ") del cliente " << nick << ". Reiniciando reensamblaje." << endl;
            vc.clear();
            expected_total_pkgs = tot;
            vc.resize(tot);
            current_message_number++; // Consider as new message
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
            // The message type is at pkg[PROTOCOL_HEADER_LEN]
            char tipo = vc[0][PROTOCOL_HEADER_LEN]; 
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

            // Handle game state if a player disconnects
            if (partida.activa) {
                if (partida.jugadorO == nick) {
                    if (mapaSockets.count(partida.jugadorX))
                        enviarM(mapaSockets[partida.jugadorX], "Tu oponente se ha desconectado. Has ganado por abandono.");
                    finalizarPartida('X'); // 'X' wins by 'O' abandonment
                } else if (partida.jugadorX == nick) {
                    if (mapaSockets.count(partida.jugadorO))
                        enviarM(mapaSockets[partida.jugadorO], "Tu oponente se ha desconectado. Has ganado por abandono.");
                    finalizarPartida('O'); // 'O' wins by 'X' abandonment
                }
                // Remove from spectators if they were one
                partida.espectadores.erase(std::remove(partida.espectadores.begin(), partida.espectadores.end(), nick), partida.espectadores.end());
            }
            // Remove from waiting list if they were there
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
        // Initial registration packet 'N'
        // Format: [seq(5)][tot(5)][len(5)]'N'[len_nick(5)][nick(N)]
        
        // Minimum length for a registration packet (1 for seq + 1 for tot + 1 for len + 1 for type + 1 for len_nick + at least 1 for nick)
        // 5 + 5 + 5 + 1 + 5 = 21 (minimum for header fields, len_nick field is 5, but nick itself must be > 0)
        
        if (pkg.length() >= PROTOCOL_HEADER_LEN + LEN_FIELD_LEN + 1 && // 1 for at least one char in nickname
            pkg.substr(0, SEQ_LEN) == "00001" && // seq should be 1
            pkg.substr(SEQ_LEN, TOT_LEN) == "00001" && // tot should be 1
            pkg[PROTOCOL_HEADER_LEN] == 'N') { // Type 'N'
            
            // Offset for len_nick field: after seq(5) + tot(5) + len(5) + type(1)
            int len_nick_offset = PROTOCOL_HEADER_LEN + TYPE_LEN;
            int tamNick = stoi(pkg.substr(len_nick_offset, LEN_FIELD_LEN));
            
            if (tamNick > 0 && (size_t)(len_nick_offset + LEN_FIELD_LEN + tamNick) <= pkg.length()) {
                string nick = pkg.substr(len_nick_offset + LEN_FIELD_LEN, tamNick);
                
                lock_guard<mutex> lock(mapaMutex);
                if (mapaSockets.find(nick) == mapaSockets.end()) {
                    mapaSockets[nick] = client_sock;
                    cout << "Cliente conectado: " << nick << endl;
                    thread(manejarCliente, client_sock, nick).detach();
                } else {
                    // Send an error message back to the client
                    char errPkt[500];
                    memset(errPkt, '#', 500);
                    sprintf(errPkt, "%05d%05d", 1, 1); // seq, tot
                    int offset = FRAGMENTATION_HEADER_LEN;
                    sprintf(errPkt + offset, "%05d", TYPE_LEN + TYPE_LEN + LEN_FIELD_LEN + 39); // len: 'E' + '1' + len_msg + "Nickname ya en uso..."
                    offset += LEN_FIELD_LEN;
                    errPkt[offset++] = 'E'; // Error type
                    errPkt[offset++] = '1'; // Error code: Nickname already in use
                    sprintf(errPkt + offset, "%05d", 39); // len of error message
                    offset += LEN_FIELD_LEN;
                    memcpy(errPkt + offset, "Nickname ya en uso. Por favor, elige otro.", 39);
                    write(client_sock, errPkt, 500);
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
