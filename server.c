/* Server code in C */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    struct sockaddr_in stSockAddr;
    int SocketSD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    char buffer[256];
    int n;
    
    if (-1 == SocketSD)
    {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(5100);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (-1 == bind(SocketSD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("error bind failed");
        close(SocketSD);
        exit(EXIT_FAILURE);
    }
    
    if (-1 == listen(SocketSD, 10))
    {
        perror("error listen failed");
        close(SocketSD);
        exit(EXIT_FAILURE);
    }
    
    for (;;)
    {
        int ConnectFD = accept(SocketSD, NULL, NULL);
        if (0 > ConnectFD)
        {
            perror("error accept failed");
            close(SocketSD);
            exit(EXIT_FAILURE);
        }
        
        do {
            n = read(ConnectFD, buffer, 255);
            buffer[n] = '\0';
            if (n < 0) perror("ERROR reading from socket");
            printf("Here is the message: [%s]\n", buffer);
            n = write(ConnectFD, "I got your message", 18);
            if (n < 0) perror("ERROR writing to socket");
        } while(strcmp(buffer, "chau") != 0);
        
        shutdown(ConnectFD, SHUT_RDWR);
        close(ConnectFD);
    }
    
    close(SocketSD);
    return 0;
}