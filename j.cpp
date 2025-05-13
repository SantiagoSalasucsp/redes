  /* Client code in C */
 
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <cstring>
  #include <string>
  #include <iostream>
  #include <sstream>
  #include <unistd.h>
  #include <vector>
  #include <thread>
  #include <fstream>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <algorithm>  
  
  using namespace std;
  
char simbol_player;
  
  
  // convert int to string with leading zeros
  
  string i_to_s(string size_element,int size){
      if(size_element.size() < size){
          return string(size - size_element.size(), '0') + size_element;
      } else if (size_element.size() > size){
          return size_element.substr(size_element.size() - size, size);
      }
      return size_element;
  }   
  
 
  // --------------------------------------------
  
     
  int main(void){
      struct sockaddr_in stSockAddr;
      int Res;
      int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
      int n;
   
      if (-1 == SocketFD)
      {
        perror("cannot create socket");
        exit(EXIT_FAILURE);
      }
   
      memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
   
      stSockAddr.sin_family = AF_INET;
      stSockAddr.sin_port = htons(48080);
      Res = inet_pton(AF_INET, "172.16.16.149", &stSockAddr.sin_addr);
   
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
      
        int contador = 0;
          char buffer[1024];
    
          char command;
          for(;;){
            bzero(buffer,1023);
             
              
            int n = read(SocketFD,buffer,1);
            buffer[n]='\0';
            if(buffer[0] == 'M'){
                n = read(SocketFD,buffer,6);
                if(n == 6 ){
                    buffer[n]='\0';
                    int size_m_1 = atoi(buffer);
                    n = read(SocketFD,buffer,size_m_1);
                    if( n == size_m_1){
                        buffer[n]='\0';
                        string m_1(buffer);
                        
                        //-----------------------
                        
                        n = read(SocketFD,buffer,3);
                        if(n == 3) {
                            buffer[n]='\0';
                            int size_m_2 = atoi(buffer);
        
                            n = read(SocketFD,buffer,size_m_2);
                            if(n == size_m_2){
                                buffer[n]='\0';
                                string m_2(buffer);
                                
                                //-----------------------
                                n = read(SocketFD,buffer,7);
    
                                if (n == 7){
                                    buffer[n]='\0';
                                    int size_m_3 = atoi(buffer);
                
                                    n = read(SocketFD,buffer,size_m_3);
                                    if (n == size_m_3){
                                        buffer[n]='\0';
                                        string m_3(buffer);
                                        
                                        //-----------------------
                                        n = read(SocketFD,buffer,1);
                                        if(n == 1){
                                            buffer[n]='\0';
                                            int size_m_4 = atoi(buffer);
                        
                                            n = read(SocketFD,buffer,size_m_4);
                                            if(n == size_m_4){
                                                buffer[n]='\0';
                                                string m_4(buffer);
                                                contador++;
                                                
                                                //-----------------------
                                            }
                                        }
                                        
                                    }
                                    
                                }
                                
                            }
                        }
                    }
                    
                }
            }
            else if (buffer[0]=='F') {
    
                string msg = to_string(contador);
                string size_msg = i_to_s(to_string(msg.size()),10);
                
                string apellido = "TITO";
                string size_apellido = i_to_s(to_string(apellido.size()),10);
    
                string to_send = "R" + size_msg + msg + size_apellido + apellido;
                
                int m = write(SocketFD,to_send.c_str(),to_send.size());
                cout<<to_send<<endl;
            }
            
            
      
            if (n < 0) perror("ERROR reading from socket");   
          }
      

  }
