/* Server code in C++ with threads handling multiple clients concurrently */
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <string>
#include <thread>
#include <mutex>

std::mutex mtx; // Mutex to avoid concurrent access to shared resources

// Function to handle communication with the client
void handleClient(int connectFD) {
    char buffer[256];
    int n;

    while (true) {
        n = read(connectFD, buffer, 255);
        buffer[n] = '\0';
        if (n < 0) {
            perror("ERROR reading from socket");
            break;
        }

        // Print received message
        mtx.lock();  // Locking to avoid concurrent access
        std::cout << "Received: " << buffer << std::endl;
        mtx.unlock(); // Unlock after printing

        // If the message is "chau", stop communication
        if (strcmp(buffer, "chau") == 0) {
            break;
        }

        // Send a response back to the client
        const char* response = "I got your message";
        n = write(connectFD, response, strlen(response));
        if (n < 0) {
            perror("ERROR writing to socket");
            break;
        }
    }

    shutdown(connectFD, SHUT_RDWR);
    close(connectFD);
}

int main() {
    struct sockaddr_in stSockAddr;
    int SocketSD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (SocketSD == -1) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(5100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(SocketSD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)) == -1) {
        perror("Error bind failed");
        close(SocketSD);
        exit(EXIT_FAILURE);
    }

    if (listen(SocketSD, 10) == -1) {
        perror("Error listen failed");
        close(SocketSD);
        exit(EXIT_FAILURE);
    }

    std::cout << "Server is waiting for connections..." << std::endl;

    while (true) {
        int ConnectFD = accept(SocketSD, NULL, NULL);
        if (ConnectFD < 0) {
            perror("Error accept failed");
            close(SocketSD);
            exit(EXIT_FAILURE);
        }

        // Create a thread to handle each client independently
        std::thread clientThread(handleClient, ConnectFD);
        clientThread.detach();  // Detach the thread to handle the client independently
    }

    close(SocketSD);
    return 0;
}
