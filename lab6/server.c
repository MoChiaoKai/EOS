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
#include <signal.h> 

#define BALANCE_FILE "balance.txt"
#define SEM_MODE 0666 /* rw(owner)-rw(group)-rw(other) permission */
#define SEM_KEY 314512057

int sem;
int socketfd;
int sem_id;   

void cleanup_handler(int signum) {
    if (signum == SIGINT) {
        semctl(sem_id, 0, IPC_RMID, 0);
        exit(EXIT_SUCCESS); 
    }
}

int P (int s){
    struct sembuf sop; z
    sop.sem_num = 0;
    sop.sem_op = -1; // acquire
    sop.sem_flg = 0; 

    if (semop (s, &sop, 1) < 0) {
        fprintf(stderr,"P(): semop failed: %s\n",strerror(errno));
        return -1;
    } 
    else {
        return 0;
    }
}
    
int V(int s){
    struct sembuf sop;
    sop.sem_num = 0; 
    sop.sem_op = 1; // release
    sop.sem_flg = 0;

    if (semop(s, &sop, 1) < 0) {
        fprintf(stderr,"V(): semop failed: %s\n",strerror(errno));
        return -1;
    } 
    else {
        return 0;
    }
}

int operations(int amount){
    int fd;
    int current;
    char buffer[100];
    int ret;

    P(sem);

    // critical section
    fd = open(BALANCE_FILE, O_RDWR);

    if (fd < 0){
        if (errno == ENOENT){
            fd = open(BALANCE_FILE, O_RDWR | O_CREAT, 0666);
        }
    }

    memset(buffer, 0, sizeof(buffer));

    ret = read(fd, buffer, sizeof(buffer));
    if (ret <= 0){
        current = 0;
    }
    else {
        current = atoi(buffer);
    }

    int new_balance = current + amount;
    lseek(fd, 0, SEEK_SET);

    ftruncate(fd, 0);

    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "%d\n", new_balance);
    write(fd, buffer, strlen(buffer));

    close(fd);
    // critical section
    V(sem);
    return new_balance;
} 

void handle_client(int connfd) {
    char buffer[256];
    ssize_t nbytes;

    nbytes = read(connfd, buffer, sizeof(buffer) - 1);
   
    buffer[nbytes] = '\0';

    char operation[20];
    int amount;
    int times;

    if (sscanf(buffer, "%s %d %d", operation, &amount, &times) != 3) {
        dprintf(connfd, "Invalid request format\n");
        close(connfd);
        exit(EXIT_FAILURE);
    }

    int actual_amount;
    int new_balance;

    if (strcmp(operation, "deposit") == 0){
        actual_amount = amount;
    }
    else if (strcmp(operation, "withdraw") == 0){
        actual_amount = -amount;
    }

    new_balance = operations(actual_amount);

    memset(buffer, 0, sizeof(buffer));

    if (new_balance != -1){
        if (actual_amount > 0){
            sprintf(buffer, "After deposit: %d\n", new_balance);
        }
        else {
            sprintf(buffer, "After withdraw: %d\n", new_balance);
        }
        printf("%s", buffer);
    }

    close(connfd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv){
    pid_t childpid;
    int status;

    signal(SIGINT, cleanup_handler);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    
    socketfd = socket(AF_INET, SOCK_STREAM, 0); //IPV4,TCP 

    if (socketfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
        
    //當socket在TIME_WAIT狀態時,允許立即重啟並重用ip和port
    int yes = 1;
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt failed"); 
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // 監聽所有介面
    serv_addr.sin_port = htons(port);
        
    if (bind(socketfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        close(socketfd);
        exit(EXIT_FAILURE);
    }
        
    if (listen(socketfd, 10) < 0) { //可以保留10個client請求
        perror("listen failed");
        close(socketfd);
        exit(EXIT_FAILURE);
    }

    /* create semaphore */
    sem = semget(SEM_KEY, 1, IPC_CREAT | IPC_EXCL | SEM_MODE);
    
    if (sem < 0){
    	if (errno == EEXIST) {
        	fprintf(stderr, "Semaphore %d already exists. Attempting to get existing one.\n", SEM_KEY);
        	sem = semget(SEM_KEY, 1, SEM_MODE); 
    	} else {
        	fprintf(stderr, "Sem %d creation failed: %s\n", SEM_KEY, strerror(errno));
        	exit(EXIT_FAILURE);
    	}
    } else {
	    if (semctl(sem, 0, SETVAL, 1) < 0) {
		fprintf(stderr, "Unable to initialize Sem: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	    }
	    printf("Semaphore %d has been created & initialized to 1\n", SEM_KEY);
	}

    sem_id = sem;

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
            
        int connfd = accept(socketfd, (struct sockaddr *) &cli_addr, &clilen);
            
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
            close(socketfd); 
            handle_client(connfd);
            exit(EXIT_SUCCESS); 
        } else { 
            close(connfd); 
            }
    }
    return 0;
}