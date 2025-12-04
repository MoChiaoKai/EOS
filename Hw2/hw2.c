#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#define MAX_SIZE 256

static time_t time_update;
int waiters[2];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


typedef struct {
    char* name;
    int dist;
} Shopinfo;

Shopinfo shops[] = {
    {"Dessert shop", 3},
    {"Beverage shop" , 5},
    {"Diner", 8}
};

typedef struct {
    char* names;
    int prices;
    int shop_index;
} Iteminfo;

Iteminfo items[] = {
    {"cookie", 60, 0},
    {"cake", 80, 0},
    {"tea", 40, 1},
    {"boba", 70, 1},
    {"fried-rice", 120, 2},
    {"Egg-drop-soup", 50, 2}
};

typedef struct {
    int shop;
    int connfd;
    int item[6];
} Clientinfo;

void client_init(Clientinfo* client, int connfd){
    client->shop = -1;
    client->connfd = connfd;
    for (int i = 0; i < 6; i++){
        client->item[i] = 0;
    }
}

typedef struct {
    int total_wait;
    int waiter_index;
    int order_time;
} Waitinfo;

void trim_newline(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

void send_msg(int sockfd, const char *text){
    char buf[MAX_SIZE];
    size_t text_len = strlen(text);
    size_t len_to_copy = (text_len < MAX_SIZE) ? text_len : MAX_SIZE;

    strncpy(buf, text, len_to_copy);

    memset(buf + text_len, 0, MAX_SIZE - text_len);

    send(sockfd, buf, MAX_SIZE, 0); 
}

ssize_t recv_msg(int sockfd, char *out, size_t out_sz) {
    char buf[MAX_SIZE];
    ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
    if (n <= 0) return n;      

    size_t len = (n < (ssize_t)(out_sz - 1)) ? (size_t)n : (out_sz - 1);
    memcpy(out, buf, len);
    out[len] = '\0';         

    trim_newline(out);
    return n;
}

void update_time(void){
    time_t now = time(NULL);
    if (now <= time_update){
        return;
    }

    int delta = (int)(now - time_update);
    for (int i = 0; i < 2; i++){
        waiters[i] -= delta;
        if (waiters[i] < 0){
            waiters[i] = 0;
        }
    }
    time_update = now;
}

Waitinfo calculate_wait_time(Clientinfo* client, int commit){
    Waitinfo waiter = {0, -1, 0};
    int delivery_time = 0;

    if (client->shop != -1){
        delivery_time = shops[client->shop].dist;
        waiter.order_time = delivery_time;
    }

    pthread_mutex_lock(&mutex); 

    update_time();

    if (waiters[0] <= waiters[1]){
        waiter.waiter_index = 0;
        waiter.total_wait = waiters[0] + delivery_time;
    }
    else {
        waiter.waiter_index = 1;
        waiter.total_wait = waiters[1] + delivery_time;
    }

    if (commit){
        waiters[waiter.waiter_index] = waiter.total_wait;
    }

    pthread_mutex_unlock(&mutex);
    return waiter;
}

void get_current_order(Clientinfo* client, char* buffer){
    buffer[0] = '\0';

    int first = 1;

    for (int i = 0; i < 6; i++){
        if (client->item[i] > 0){
            if (!first){
                strcat(buffer, "|");
            }

            char item_info[50];
            sprintf(item_info, "%s %d", items[i].names, client->item[i]);
            strcat(buffer, item_info);
            first = 0;
        }
    }
}

void handle_shop_list(Clientinfo* client) {
    char list_buf[MAX_SIZE];
    snprintf(list_buf, sizeof(list_buf), 
         "Dessert shop:3km\n"
         "- cookie:$60|cake:$80\n"
         "Beverage shop:5km\n"
         "- tea:$40|boba:$70\n"
         "Diner:8km\n"
         "- fried-rice:$120|Egg-drop-soup:$50\n");
    
    send_msg(client->connfd, list_buf);
}


void handle_order(Clientinfo* client, char* details ){
    char item_name[20];
    int quantity;
    char buffer[MAX_SIZE];

    sscanf(details, "%s %d", item_name, &quantity);

    int index = -1;

    for (int i = 0; i < 6; i++){
        if (strcmp(items[i].names, item_name) == 0){
            index = i;
            break;
        }
    }

    if (index == -1){
        return;
    }

    if (client->shop == -1){
        client->shop = items[index].shop_index;
    }
    else if (client->shop != items[index].shop_index){
        get_current_order(client, buffer);
        send_msg(client->connfd, buffer);
        return;
    }
    client->item[index] += quantity;
    
    get_current_order(client, buffer);
    send_msg(client->connfd, buffer);

    return;
}

int handle_comfirm(Clientinfo* client){
    int total_price = 0;

    if (client->shop ==-1){
        send_msg(client->connfd, "Please order some meals\n");
        return 0;
    }

    int delivery_time = shops[client->shop].dist;
    
    for (int i = 0; i < 6; i++){
        total_price += client->item[i] * items[i].prices;
    }
    Waitinfo initwait = calculate_wait_time(client, 0);

    int wait_time = initwait.total_wait;

    int waiter_index = initwait.waiter_index;

    if (wait_time >= 30){
        send_msg(client->connfd, "Your delivery will take a long time, do you want to wait?");
        char response[MAX_SIZE];
        ssize_t nbytes = recv_msg(client->connfd, response, sizeof(response) - 1);
        if (nbytes <= 0){
            return 0;
        }
        response[nbytes] = '\0';
        if (strcmp(response, "Yes") != 0 && strcmp(response, "yes") != 0){
            return 1;
        }
    }

    send_msg(client->connfd, "Please wait a few minutes...");
    
    pthread_mutex_lock(&mutex);
    update_time();


    if (waiters[0] <= waiters[1]){
        waiters[0] += delivery_time;
        wait_time = waiters[0];
    }
    else {
        waiters[1] += delivery_time;
        wait_time = waiters[1];
    }

    pthread_mutex_unlock(&mutex);
            
    sleep(wait_time);

    char confirm_msg[MAX_SIZE]; 
    snprintf(confirm_msg, sizeof(confirm_msg), "Delivery has arrived and you need to pay %d$\n", total_price);
    send_msg(client->connfd, confirm_msg);
    return 1;
}

void* handle_client(void* arg){
    int *p_connfd = (int*)arg;
    int connfd = *p_connfd;
    
    Clientinfo* client = (Clientinfo*)malloc(sizeof(Clientinfo));
    client_init(client, connfd);

    while(1){
        char buffer[MAX_SIZE];
        char command[20];
        ssize_t nbytes;
        nbytes = recv_msg(client->connfd, buffer, sizeof(buffer) - 1);

        sscanf(buffer, "%s", command);

        if (strcmp(command, "shoplist") == 0){
            handle_shop_list(client);
        }
        else if (strcmp(command, "order") == 0){
            handle_order(client, buffer + 6);
        }
        else if (strcmp(command, "comfirm") == 0){
            if (handle_comfirm(client)){
                break;
            };
        }
        else if (strcmp(command, "cancel") == 0){
            break;
        }
    }

    close(client->connfd);
    free(client);
    free(p_connfd);
    pthread_exit(NULL);
}

int main(int argc, char **argv){
    int socketfd;
    pthread_t tid;

    pthread_mutex_init(&mutex, NULL);
    time_update = time(NULL);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    
    socketfd = socket(AF_INET, SOCK_STREAM, 0);

    if (socketfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
        
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

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        
        int *p_connfd = malloc(sizeof(int));
        if (p_connfd == NULL) {
            perror("malloc failed");
            continue;
        }
        
        *p_connfd = accept(socketfd, (struct sockaddr *) &cli_addr, &clilen);
            
        if (*p_connfd < 0) {
            // 如果是被 Signal 中斷，重新回到迴圈頂部
            if (errno == EINTR) { 
            	free(p_connfd);
                continue;
            }
            free(p_connfd);
            perror("accept failed");
            continue; // 遇到其他錯誤，繼續迴圈
        }

        if (pthread_create(&tid, NULL, handle_client, (void*)p_connfd) != 0) {
            perror("Thread create failed");
            close(*p_connfd);
            free(p_connfd);
            continue;
        }
        pthread_detach(tid);
    }
    return 0;
}