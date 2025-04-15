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
#include <algorithm>

using namespace std;

/* FUNCIONES ADICIONALES */
string nickname;

void N_funcion(int socketFD) {
    char buffer[256];
    printf("Nickname: ");
    fgets(buffer, 100, stdin);
    buffer[strcspn(buffer, "\n")] = '\0';
    nickname = buffer;
    
    int message_size = nickname.length() + 1;
    sprintf(buffer, "%05dN%s", message_size, nickname.c_str());
    write(socketFD, buffer, 5 + message_size);
}

void L_funcion(int socketFD) {
    char buffer[10];
    sprintf(buffer, "%05dL", 1);
    write(socketFD, buffer, 6);
}

void M_funcion(int socketFD) {
    char recipient[100], message[900];
    printf("Destinatario: ");
    fgets(recipient, 100, stdin);
    recipient[strcspn(recipient, "\n")] = '\0';
    
    printf("Mensaje: ");
    fgets(message, 900, stdin);
    message[strcspn(message, "\n")] = '\0';
    
    char buffer[1024];
    int recipient_size = strlen(recipient);
    int total_size = 1 + 5 + recipient_size + strlen(message);
    sprintf(buffer, "%05dM%05d%s%s", total_size, recipient_size, recipient, message);
    write(socketFD, buffer, 5 + total_size);
}

void B_funcion(int socketFD) {
    char message[900];
    printf("Mensaje para todos: ");
    fgets(message, 900, stdin);
    message[strcspn(message, "\n")] = '\0';
    
    // Formato: [5B tamaño total][B][5B tamaño mensaje][mensaje]
    int msg_size = strlen(message);
    char buffer[1024];
    sprintf(buffer, "%05dB%05d%s", 1 + 5 + msg_size, msg_size, message);
    write(socketFD, buffer, 5 + 1 + 5 + msg_size);
}

void F_funcion(int socketFD) {
    char destination[100], filename[100];
    
    printf("Destinatario ('all' para todos): ");
    fgets(destination, 100, stdin);
    destination[strcspn(destination, "\n")] = '\0';
    
    printf("Ruta del archivo: ");
    fgets(filename, 100, stdin);
    filename[strcspn(filename, "\n")] = '\0';
    
    // Abrir el archivo
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("Archivo no encontrado: %s\n", filename);
        return;
    }
    
    // Obtener tamaño del archivo
    fseek(fp, 0, SEEK_END);
    int filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Crear un hash simple (suma de bytes)
    int hash = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        hash = (hash + c) % 100000; // Para mantener 5 dígitos
    }
    fseek(fp, 0, SEEK_SET);
    
    // Obtener solo el nombre del archivo sin la ruta
    char* basename = strrchr(filename, '/');
    if (basename) {
        basename++; // Saltar el '/'
    } else {
        basename = filename; // No hay '/' en la ruta
    }
    
    // Tamaño total del mensaje
    int dest_size = strlen(destination);
    int total_size = 1 + 5 + dest_size + 100 + 5 + filesize + 5;
    
    char buffer[1024];
    
    // Enviar encabezado
    sprintf(buffer, "%05dF%05d%s", total_size, dest_size, destination);
    write(socketFD, buffer, 5 + 1 + 5 + dest_size);
    
    // Enviar nombre del archivo (100B)
    int name_len = strlen(basename);
    if (name_len > 100) name_len = 100;
    write(socketFD, basename, name_len);
    // Padding para completar 100B
    char padding[100] = {0};
    write(socketFD, padding, 100 - name_len);
    
    // Enviar tamaño del archivo
    sprintf(buffer, "%05d", filesize);
    write(socketFD, buffer, 5);
    
    // Enviar contenido del archivo
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, 1024, fp)) > 0) {
        write(socketFD, buffer, bytes_read);
    }
    
    // Enviar hash
    sprintf(buffer, "%05d", hash);
    write(socketFD, buffer, 5);
    
    fclose(fp);
    printf("Archivo %s enviado a %s\n", basename, destination);
}

void Q_funcion(int socketFD) {
    char buffer[10];
    sprintf(buffer, "%05dQ", 1);
    write(socketFD, buffer, 6);
}

void showMenu() {
    printf("\nOpciones:\n");
    printf("L. Listar usuarios\n");
    printf("M. Enviar mensaje\n");
    printf("B. Enviar mensaje a todos\n");
    printf("Q. Salir\n");
    printf("F. Enviar archivo\n");
    printf("Seleccione opción: ");
    fflush(stdout);
}

void readSocketThread(int cliSocket) {
    char buffer[1024];
    int n;
    
    while(true) {
        // Read the message size (5 bytes)
        n = read(cliSocket, buffer, 5);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        int message_size = atoi(buffer);
        
        // Read the message type (1 byte)
        n = read(cliSocket, buffer, 1);
        if (n <= 0) break;
        
        char message_type = buffer[0];
        
        if (message_type == 'l') {
            // List response
            n = read(cliSocket, buffer, message_size - 1);
            if (n <= 0) break;
            
            buffer[n] = '\0';
            printf("\nUsuarios: %s\n", buffer);
        }
        else if (message_type == 'm') {
            // Direct message
            n = read(cliSocket, buffer, 5);
            if (n <= 0) break;
            
            buffer[n] = '\0';
            int sender_size = atoi(buffer);
            
            char sender[100];
            n = read(cliSocket, sender, sender_size);
            if (n <= 0) break;
            
            sender[n] = '\0';
            
            char msg[900];
            n = read(cliSocket, msg, message_size - 1 - 5 - sender_size);
            if (n <= 0) break;
            
            msg[n] = '\0';
            printf("\nMensaje de %s: %s\n", sender, msg);
        }
        else if (message_type == 'b') {
            // Broadcast message - Formato: [5B tamaño][b][5B tamaño mensaje][mensaje][5B tamaño nickname][nickname]
            n = read(cliSocket, buffer, 5); // Leer tamaño mensaje
            if (n <= 0) break;
            buffer[n] = '\0';
            int msg_size = atoi(buffer);
            
            char msg[900];
            n = read(cliSocket, msg, msg_size);
            if (n <= 0) break;
            msg[n] = '\0';
            
            n = read(cliSocket, buffer, 5); // Leer tamaño nickname
            if (n <= 0) break;
            buffer[n] = '\0';
            int nick_size = atoi(buffer);
            
            char sender[100];
            n = read(cliSocket, sender, nick_size);
            if (n <= 0) break;
            sender[n] = '\0';
            
            printf("\nMensaje para todos de %s: %s\n", sender, msg);
        }
        else if (message_type == 'F') {
            // Recibir archivo del servidor
            n = read(cliSocket, buffer, 5); // Leer tamaño del origen
            if (n <= 0) break;
            buffer[n] = '\0';
            int origin_size = atoi(buffer);
            
            char origin[100];
            n = read(cliSocket, origin, origin_size);
            if (n <= 0) break;
            origin[n] = '\0';
            
            // Leer nombre del archivo (100B)
            char filename[101];
            n = read(cliSocket, filename, 100);
            if (n <= 0) break;
            filename[n] = '\0';
            
            // Asegurar que el nombre del archivo termina en NULL
            for (int i = 0; i < 100; i++) {
                if (filename[i] == 0) break;
                if (i == 99) filename[i] = 0;
            }
            
            // Crear path para guardar el archivo con prefijo "received_"
            char save_path[120];
            sprintf(save_path, "received_%s", filename);
            
            // Crear el archivo
            FILE* fp = fopen(save_path, "wb");
            if (!fp) {
                printf("\nError al crear archivo %s\n", save_path);
                // Leer y descartar el resto del mensaje
                int remaining = message_size - 1 - 5 - origin_size - 100;
                while (remaining > 0) {
                    n = read(cliSocket, buffer, min(1024, remaining));
                    if (n <= 0) break;
                    remaining -= n;
                }
                continue;
            }
            
            // Leer el tamaño del archivo
            n = read(cliSocket, buffer, 5);
            if (n <= 0) {
                fclose(fp);
                break;
            }
            buffer[n] = '\0';
            int filesize = atoi(buffer);
            
            // Leer el contenido del archivo
            int bytes_read = 0;
            printf("\nRecibiendo archivo %s de %s (%d bytes)...\n", filename, origin, filesize);
            
            while (bytes_read < filesize) {
                n = read(cliSocket, buffer, min(1024, filesize - bytes_read));
                if (n <= 0) break;
                fwrite(buffer, 1, n, fp);
                bytes_read += n;
                
                // Mostrar progreso
                printf("\rProgreso: %d%%", (bytes_read * 100) / filesize);
                fflush(stdout);
            }
            
            // Leer el hash
            n = read(cliSocket, buffer, 5);
            if (n <= 0) {
                fclose(fp);
                break;
            }
            
            fclose(fp);
            printf("\nArchivo recibido y guardado como %s\n", save_path);
        }
        
        showMenu();
    }
    
    printf("\nDesconectado del servidor.\n");
    close(cliSocket);
    exit(0);
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[1024];

    if (-1 == SocketFD) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45002);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in))) {
        perror("Connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    N_funcion(SocketFD);
    std::thread(readSocketThread, SocketFD).detach();

    char option;
    do {
        showMenu();
        scanf(" %c", &option);
        getchar(); // Consume newline
        
        switch(option) {
            case 'L':
            case 'l':
                L_funcion(SocketFD);
                break;
                
            case 'M':
            case 'm':
                M_funcion(SocketFD);
                break;
                
            case 'B':
            case 'b':
                B_funcion(SocketFD);
                break;
                
            case 'Q':
            case 'q':
                Q_funcion(SocketFD);
                printf("Desconectando...\n");
                break;
                
            case 'F':
            case 'f':
                F_funcion(SocketFD);
                break;
                
            default:
                printf("Opción inválida\n");
        }
    } while (option != 'Q' && option != 'q');

    close(SocketFD);
    return 0;
}