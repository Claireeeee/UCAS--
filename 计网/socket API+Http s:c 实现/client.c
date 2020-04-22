/* client application */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in server;
    char message[1000];   //message->sendbuf
    
    
    sprintf(message, "GET /hell HTTP/1.1\r\n");
    strcat(message, "Host: 10.0.0.1\r\n");
    strcat(message,"Connection: Keep-Alive\r\n");
    
    // create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    //printf("socket: %d\n",sock);
    if (sock == -1)
    {
        printf("create socket failed");
        return -1;
    }
    printf("socket created\n");
    
    server.sin_addr.s_addr = inet_addr("10.0.0.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(80);
    
    // connect to server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect failed");
        return 1;
    }
    
    printf("connected\n");
    int i=0;
    int buf_size=65536;
    for(;i<1;i++){
        printf("requesting...\n");
        //scanf("%s", message);
        
        // send http request
        if (send(sock, message, strlen(message), 0) < 0) {
            printf("send failed");
            return 1;
        }
 	    shutdown(sock, SHUT_WR); 
        // receive
        int fd = open("hello-copy", O_CREAT | O_WRONLY, S_IRWXG | S_IRWXO | S_IRWXU);
        if (fd < 0)
        {
            printf("Create file failed\n");
            return 0;
        }
        char *buf = (char *) malloc(buf_size * sizeof(char));
        //从套接字中读取文件流
        int len;
	int j=0;
	while((len = recv(sock, buf, buf_size,0))>0){
	  if(j==1) 
		printf("CONNECTED. RECEIVING......\n");
	  j++;
          write(fd, buf, len);
        }
    }
    close(sock);
    return 0;
}

