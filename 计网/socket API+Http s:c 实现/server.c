#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

void reqparse(int);
void headers(int, const char *);
void not_found(int);
int socket_init(u_short *);


void unimplemented(int);
int get_line(int, char *, int);

int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    
    /*把终止条件统一为 \n 换行符，标准化 buf 数组*/
    while ((i < size - 1) && (c != '\n'))
    {
        /*一次仅接收一个字节*/
        n = (int)recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            /*收到 \r 则继续接收下个字节，因为换行符可能是 \r\n */
            if (c == '\r')
            {
                /*使用 MSG_PEEK 标志使下一次读取依然可以得到这次读取的内容，可认为接收窗口不滑动*/
                n = (int)recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                /*但如果是换行符则把它吸收掉*/
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            /*存到缓冲区*/
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
    
    /*返回 buf 数组大小*/
    return(i);
}

void not_found(int client)
{
    char buf[1024];
    /* 404 页面 */
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
}

void unimplemented(int client)
{
    char buf[1024];
    /* HTTP method 不被支持*/
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
}

void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */
    
    /*正常的 HTTP header */
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    strcpy(buf, "Server: 10.0.0.1\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void reqparse(int client)
{
    char buf[1024];
    int query_len;
    char method[255];
    char url[255];
    //char path[512];
    
    size_t i, j;
    struct stat st;
    
    query_len = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    //打印请求信息
    printf("REQUESTING: %s",buf);
    while (!isspace((int)buf[j]))
    {
        method[i] = buf[j];
        i++; j++;
    }
    method[i] = '\0';
    
    //如果请求方法不是 GET
    if (strcasecmp(method, "GET"))
    {
        unimplemented(client);
        return;
    }
    //解析url
    i = 0;
    while (isspace((int)buf[j]) && (j < sizeof(buf)))
        j++;
    j++;
    while (!isspace((int)buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';
    
    //未指明文件时，返回hello文件
    if ((url[0] == '\0')){
        strcat(url, "hello");
        //printf("empty\n");
    }
    //文件查找
    while ((query_len > 0) && strcmp("\n", buf))  /* read & discard headers */
        query_len = get_line(client, buf, sizeof(buf));
    
    if (stat(url, &st)==-1){
        not_found(client);
    }
    //文件传输
    else
    {
        FILE *resource = NULL;
	    resource=fopen(url, "r");
        if (resource == NULL)
            not_found(client);
        else
            {
                headers(client, url);
                char bufa[1024];
                fgets(bufa, sizeof(bufa), resource);
                while (!feof(resource))
                {
                    send(client, bufa, strlen(bufa), 0);
                    fgets(bufa, sizeof(bufa), resource);
                }
            }
        fclose(resource);
    }
    close(client);
}

int socket_init(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;
    
    //建立 socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
    {
        perror("socket");
        exit(1);  
    }
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    {
        perror("bind");
        exit(1);  
    }
    //监听
    if (listen(httpd, 5) < 0)
    {
        perror("listen");
        exit(1);  
    }
    return(httpd);
}

int main(void)
{
    int server_sock = -1;
    u_short port = 80;
    int client_sock = -1;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
    pthread_t newthread;
    
    server_sock = socket_init(&port);
    printf("httpd running on port %d\n", port);
    while (1)
    {
        //接收客户端连接请求
        client_sock = accept(server_sock,(struct sockaddr *)&client_name,(socklen_t *)&client_name_len);
        if (client_sock == -1)
        {
            perror("accept");
            exit(1);
        }
        //派生新线程用 accept_request 函数处理新请求
        if (pthread_create(&newthread , NULL, reqparse, client_sock) != 0)
            perror("pthread_create");
    }
    close(server_sock);
    return(0);
}

