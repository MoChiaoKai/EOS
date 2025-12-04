#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>


int main(int argc, char *argv[]){
    int socketfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    char request[256];

    if (argc < 6){
        fprintf(stderr,"Usage %s <ip> <port> <deposit/withdraw> <amout> <times>\n", argv[0]);
        exit(0);
    }

    char *ip = argv[1];
    int portnum = atoi(argv[2]);
    char *op  = argv[3];
    int amount = atoi(argv[4]);
    int times = atoi(argv[5]);

    if (strcmp(op,"deposit") !=0 && strcmp(op,"withdraw") !=0){
        fprintf(stderr,"Operation must be deposit or withdraw\n");
        exit(0);
    }

    for (int i = 0; i < times; i++){
        socketfd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketfd < 0){
            perror("ERROR opening socket");
            exit(0);
        }

        server = gethostbyname(ip);

        if (server == NULL){
            fprintf(stderr,"ERROR, no such host\n");
            exit(0);
        }

        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        serv_addr.sin_port = htons(portnum);

        if (connect(socketfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
            perror("ERROR connecting");
            exit(0);
        }

        sprintf(request, "%s %d %d\n", op, amount, times);

        int n = write(socketfd, request, strlen(request));
        if (n < 0){
            perror("ERROR writing to socket");
            exit(0);
        }

        memset(buffer, 0, 256);
        n = read(socketfd, buffer, 255);
        if (n < 0){
            perror("ERROR reading from socket");
            exit(0);
        }
        printf("%s", buffer);
        close(socketfd);
    }

    return 0;
}