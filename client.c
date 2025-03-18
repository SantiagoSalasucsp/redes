/* Client code in C */

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
    int Res;
    int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    int n;
    char buffer[1000];
    
    if (-1 == SocketFD)
    {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
    }
    
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(5100);
    Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);
    
    if (0 > Res)
    {
        perror("error: first parameter is not a valid address family");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    else if (0 == Res)
    {
        perror("char string (second parameter does not contain valid ipaddress");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    
    if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in)))
    {
        perror("connect failed");
        close(SocketFD);
        exit(EXIT_FAILURE);
    }
    
    n = write(SocketFD, "Hi, this is Julio.", 18);
    /* perform read write operations ... */
    
    while(1) {
        printf("Enter message (type 'chau' to quit): ");
        fgets(buffer, 1000, stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline
        
        n = write(SocketFD, buffer, strlen(buffer));
        if (n < 0) perror("ERROR writing to socket");
        
        char response[100];
        n = read(SocketFD, response, 100);
        if (n < 0) perror("ERROR reading from socket");
        response[n] = '\0';
        
        printf("%s\n", response);
        
        if (strcmp(buffer, "chau") == 0)
            break;
    }
    
    shutdown(SocketFD, SHUT_RDWR);
    close(SocketFD);
    return 0;
}