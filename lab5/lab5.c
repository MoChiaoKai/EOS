#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>     

int sockfd; 

void handler_zombie(int signum) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_client(int connfd) {
    printf("Train ID: %d\n", getpid());
    
    if (dup2(connfd, STDOUT_FILENO) == -1) {
        perror("dup2 failed");
        close(connfd); 
        exit(EXIT_FAILURE);
    }
    
    close(connfd); 

    execlp("sl", "sl", "-l", NULL);
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    
    signal(SIGCHLD, handler_zombie);     
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0); //IPV4,TCP 

    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    //當socket在TIME_WAIT狀態時,允許立即重啟並重用ip和port
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt failed"); 
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // 監聽所有介面
    serv_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    if (listen(sockfd, 10) < 0) { //可以保留10個client請求
        perror("listen failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        
        int connfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        
        if (connfd < 0) {
            // 如果是被 Signal 中斷，重新回到迴圈頂部
            if (errno == EINTR) { 
                continue;
            }
            perror("accept failed");
            continue; // 遇到其他錯誤，繼續迴圈
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            close(connfd);
        } else if (pid == 0) {
            close(sockfd); 
            handle_client(connfd);
            exit(EXIT_SUCCESS); 
        } else { 
            close(connfd); 
        }
    }

    return 0;
}