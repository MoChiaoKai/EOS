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
#define BUF_SIZE 256

//DATA struct
typedef struct {
    const char *name;
    int distance;
} ShopInfo;

typedef struct {
    const char *name;
    int price;
    int shop_id;
} ItemInfo;


ShopInfo shops[] = {
    {"Dessert shop",   3},
    {"Beverage shop",  5},
    {"Diner",          8}
};

ItemInfo items[] = {
    {"cookie",        60, 0},
    {"cake",          80, 0},
    {"tea",           40, 1},
    {"boba",          70, 1},
    {"fried-rice",   120, 2},
    {"egg-drop-soup", 50, 2}
};
//0=Dessert shop; 1=Beverage shop 2=Diner
const int ITEM_NUM = sizeof(items) / sizeof(items[0]);

/* ----------client order state ---------- */
typedef struct {
    int sockfd;              // ??client ??socket
    int chosen_shop;         // -1=撠?詨?嚗擗?0~2 撠? shops[]
    int order_cnt[6];        // ?車擗??桀?暺?撟曆遢
} ClientState;

//deliver  muxtex=======================================================
static pthread_mutex_t courier_mutex = PTHREAD_MUTEX_INITIALIZER;
static int courier_wait[2];     
static time_t last_update;

//======================================================================
void init_client_state(ClientState *st, int sockfd) {
    st->sockfd = sockfd;
    st->chosen_shop = -1;
    for (int i = 0; i < ITEM_NUM; ++i)
        st->order_cnt[i] = 0;
}

//===================================================
static void update_courier_wait_locked(void) {
    time_t now = time(NULL);              // ?曉??
    if (now <= last_update) return;       // 瘝?敺停銝??

    int delta = (int)(now - last_update); // 蝬?鈭嗾蝘?
    for (int i = 0; i < 2; ++i) {
        courier_wait[i] -= delta;         // ?挾??撌脩???
        if (courier_wait[i] < 0)
            courier_wait[i] = 0;
    }
    last_update = now;
}



void trim_newline(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}


void send_msg(int sockfd, const char *text){
    char buf[BUF_SIZE];
    memset(buf,0,sizeof(buf));

    size_t len=strlen(text);
    if (len >= BUF_SIZE){
        len= BUF_SIZE-1;
    }
    
    memcpy(buf,text,len);
    send(sockfd,buf,sizeof(buf),0);
}

ssize_t recv_msg(int sockfd, char *out, size_t out_sz) {
    char buf[BUF_SIZE];
    ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
    if (n <= 0) return n;       // 0: 撠??嚗?0: ?航炊

    size_t len = (n < (ssize_t)(out_sz - 1)) ? (size_t)n : (out_sz - 1);
    memcpy(out, buf, len);
    out[len] = '\0';           // ??銝摰?鋆?0嚗??蝣?

    while (len > 0 && out[len-1] == '\0')
        out[--len] = '\0';

    trim_newline(out);
    return n;
}

int find_item_index(const char *name){
    for(int i=0; i<ITEM_NUM ; ++i){
        if(strcmp(items[i].name, name)==0){
            return i;
        }
    }
    return -1;
}

int calc_total(const ClientState *st) {
    int sum = 0;
    for (int i = 0; i < ITEM_NUM; ++i) {
        sum += st->order_cnt[i] * items[i].price;
    }
    return sum;
}


//page disign============================================================
void handle_shop_list(ClientState *st) {
    (void)st;   


    char out[BUF_SIZE];
    snprintf(out, sizeof(out),
             "Dessert shop:3km\n"
             "- cookie:$60|cake:$80\n"
             "Beverage shop:5km\n"
             "- tea:$40|boba:$70\n"
             "Diner:8km\n"
             "- fried-rice:$120|Egg-drop-soup:$50\n");

    send_msg(st->sockfd, out);
}

void handle_order(ClientState *st, const char *args) {
    char item_name[64];
    int num;

    /* 閫???粹?暺?蝔梯??賊? */
    if (sscanf(args, "%63s %d", item_name, &num) != 2)
        return;

    int idx = find_item_index(item_name);
    if (idx < 0)
        return; 

    int shop_id = items[idx].shop_id;

    
    if (st->chosen_shop == -1) {
        st->chosen_shop = shop_id;
    }

    if (st->chosen_shop == shop_id) {
        st->order_cnt[idx] += num;
    }


    /* ??桀?撌脤???暺??殷??澆?嚗tem1 cnt1|item2 cnt2\n */
    char out[BUF_SIZE];
    out[0] = '\0';
    int first = 1;

    for (int i = 0; i < ITEM_NUM; ++i) {
        if (st->order_cnt[i] > 0) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%s %d",
                     items[i].name, st->order_cnt[i]);

            if (!first)
                strncat(out, "|", sizeof(out) - strlen(out) - 1);

            strncat(out, tmp, sizeof(out) - strlen(out) - 1);
            first = 0;
        }
    }

    strncat(out, "\n", sizeof(out) - strlen(out) - 1);
    send_msg(st->sockfd, out);
}

int handle_confirm(ClientState *st) {
    /* 瑼Ｘ????隞颱??梯正 */
    int has_order = 0;
    for (int i = 0; i < ITEM_NUM; ++i) {
        if (st->order_cnt[i] > 0) {
            has_order = 1;
            break;
        }
    }

    if (!has_order) {
        /* 瘝?擗停 confirm ??閬???擗?*/
        send_msg(st->sockfd, "Please order some meals\n");
        return 1;
    }

    /* ??擗??????葉????*/
    
    int dist = shops[st->chosen_shop].distance;
    int cid;
    int predicted_wait;

    //current deliver time
    pthread_mutex_lock(&courier_mutex);
    update_courier_wait_locked();
    cid = (courier_wait[0] <= courier_wait[1]) ? 0 : 1;
    predicted_wait = courier_wait[cid] + dist;
    pthread_mutex_unlock(&courier_mutex);
   

    
    //mutex lock ================================================
    if (predicted_wait >= 30) {
        send_msg(st->sockfd,
                 "Your delivery will take a long time, do you want to wait?\n");

        char reply[BUF_SIZE];
        ssize_t n = recv_msg(st->sockfd, reply, sizeof(reply));
        if (n <= 0) {
            return 0;
        }

        if (strcmp(reply, "Yes") != 0 &&
            strcmp(reply, "YES") != 0 &&
            strcmp(reply, "yes") != 0) {
            /* 銝 Yes 撠梁雿?No ????cancel嚗?亦???? */
            return 0;
        }
    }

    send_msg(st->sockfd, "Please wait a few minutes...\n");
    pthread_mutex_lock(&courier_mutex);
    update_courier_wait_locked();
    //cid = (courier_wait[0] <= courier_wait[1]) ? 0 : 1;
    predicted_wait = courier_wait[cid] + dist;
    courier_wait[cid] += dist; 
    pthread_mutex_unlock(&courier_mutex);
    
    //mutex unlock ================================================
    sleep(predicted_wait); 
    //mutex unlock ================================================
    /* 蝞蜇??銝血???*/
    int total = calc_total(st);
    char out[BUF_SIZE];
    snprintf(out, sizeof(out),
             "Delivery has arrived and you need to pay %d$\n",
             total);
    send_msg(st->sockfd, out);

    return 0;
}


//client thread============================================================
void *client_thread(void *arg) {
    int connfd = *(int *)arg;
    free(arg);

    ClientState st;
    init_client_state(&st, connfd);

    char buf[BUF_SIZE];

    while (1) {
        ssize_t n = recv_msg(connfd, buf, sizeof(buf));
        if (n <= 0) break;      // 撠?????

        /* 靘?誘憿??晷 */
        if (strncmp(buf, "shop list", 9) == 0) {
            handle_shop_list(&st);

        } else if (strncmp(buf, "order ", 6) == 0) {
            handle_order(&st, buf + 6);

        } else if (strcmp(buf, "confirm") == 0) {
            if (!handle_confirm(&st))    // ??擗停蝯?
                break;

        } else if (strcmp(buf, "cancel") == 0) {
            break;

        } else {
            //no command
        }
    }

    close(connfd);
    return NULL;
}


//sever==============================================================
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    //init deliver info
    courier_wait[0] = courier_wait[1] = 0;
    last_update = time(NULL);

    /*create socket */
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(port);

    if (bind(listenfd, (struct sockaddr *)&servaddr,
             sizeof(servaddr)) < 0) {
        perror("bind");
        close(listenfd);
        return 1;
    }

    if (listen(listenfd, 10) < 0) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    /* client connect create the child thread*/
    while (1) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

        int *connfd = malloc(sizeof(int));
        if (!connfd) {
            perror("malloc");
            break;
        }

        *connfd = accept(listenfd,
                         (struct sockaddr *)&cliaddr, &clilen);
        if (*connfd < 0) {
            perror("accept");
            free(connfd);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread,
                           connfd) != 0) {
            perror("pthread_create");
            close(*connfd);
            free(connfd);
            continue;
        }

        pthread_detach(tid);    // 銝?閬?join
    }

    close(listenfd);
    return 0;
}