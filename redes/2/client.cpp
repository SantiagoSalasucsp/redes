/* Client code in C++ with threads handling reading and writing */
#include <iostream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <string>

void readServer(int socketFD) {
    char response[100];
    int n;
    while (true) {
        n = read(socketFD, response, 100);
        if (n < 0) {
            perror("ERROR reading from socket");
            break;
        }
        response[n] = '\0';
        std::cout << "Response from server: " << response << std::endl;

        if (strcmp(response, "I got your message") == 0) {
            break;  // Exit the loop if the server sends this message
        }
    }
}

void writeServer(int socketFD) {
    std::string message;
    while (true) {
        std::cout << "Enter message (type 'chau' to quit): ";
        std::getline(std::cin, message);

        int n = write(socketFD, message.c_str(), message.length());
        if (n < 0) {
            perror("ERROR writing to socket");
            break;
        }

        if (message == "chau") {
            break;  // Exit the loop if "chau" is typed
        }
    }
}

int main() {
    struct sockaddr_in stSockAddr;
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (SocketFD == -1) {
        perror("Cannot create socket");
        exit(EXIT_FAILURE);
    }

    std::memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(5100);
    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

    if (Res <= 0) {
        if (Res == 0) {
            perror("Invalid IP address format");
        } else {
            perror("Error: first parameter is not a valid address family");
        }
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    if (connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)) == -1) {
        perror("Connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }

    // Launch threads for reading and writing
    std::thread readThread(readServer, SocketFD);
    std::thread writeThread(writeServer, SocketFD);

    // Wait for both threads to finish
    readThread.join();
    writeThread.join();

    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);

    return 0;
}

//g++ -std=c++11 server.cpp -o server -pthread
//g++ -std=c++11 client.cpp -o client -pthread

