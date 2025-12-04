#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define TICK_MS 10                              
#define MS_TO_TICKS(ms) ((ms + TICK_MS - 1) / TICK_MS) 

int timer_pipe[2];                            
int next_timer_id = 1;                        

typedef struct soft_timer {
    int id;                                 
    int tick_left;                             
    int interval_ticks;                       
    void (*callback)(void *);                   
    void *arg;                                  
    struct soft_timer *next;                 
} soft_timer_t;

soft_timer_t *timer_list_head = NULL;      

long get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void timer_service_routine(int signum) {
    char tick_msg = 'T';
    write(timer_pipe[1], &tick_msg, 1);
}

int st_start(int ms, int periodic, void (*cb)(void *), void *arg) {
    if (cb == NULL || ms <= 0){
        return -1;
    }
    
    soft_timer_t *new_timer = (soft_timer_t *)malloc(sizeof(soft_timer_t));
    if (new_timer == NULL){
        return -1;
    }

    int ticks = MS_TO_TICKS(ms);

    new_timer->id = next_timer_id++;
    new_timer->tick_left = ticks;
    new_timer->interval_ticks = periodic ? ticks : 0;
    new_timer->callback = cb;
    new_timer->arg = arg;
    new_timer->next = NULL;

    new_timer->next = timer_list_head;
    timer_list_head = new_timer;

    return new_timer->id;
}


int st_cancel(int id) {
    soft_timer_t *curr = timer_list_head;
    soft_timer_t *prev = NULL;

    while (curr != NULL) {
        if (curr->id == id) {
            if (prev == NULL) {
                timer_list_head = curr->next;
            } else {
                prev->next = curr->next;
            }
            printf("[ST_CANCEL] ID %d cancelled.\n", curr->id);
            free(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

void worker_loop() {
    char tick_msg;
    soft_timer_t *curr, *prev = NULL;
    
    while (1){
        read(timer_pipe[0], &tick_msg, 1);
        
        prev = NULL;
        curr = timer_list_head;

        while (curr != NULL) {
            curr->tick_left--;

            if (curr->tick_left <= 0) {
                curr->callback(curr->arg); 

                if (curr->interval_ticks > 0) {
                    curr->tick_left = curr->interval_ticks;
                    prev = curr;
                    curr = curr->next;
                } else {
                    if (prev == NULL) {
                        timer_list_head = curr->next;
                        free(curr);
                        curr = timer_list_head; 
                    } else {
                        prev->next = curr->next;
                        free(curr);
                        curr = prev->next; 
                    }
                }
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
    }
}

void callback_sleep(void *arg) {
    long id = (long)arg;
    long start_time = get_current_time();
    
    printf("\n[Callback %ld] --- START ---, TISR Activated at Timestamp: %ld\n", id, start_time);
    struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
    struct timespec rem;

    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            break; 
        }
    } 
    
    printf("[Callback %ld] --- END ---, Finished at Timestamp: %ld\n", id, get_current_time());
    printf("-------------------------------------------------------------------\n");
}

// 模擬一個快速的回調函數 (用於週期性計時器)
void callback_quick(void *arg) {
    long id = (long)arg;
    printf("[Callback %ld] Quick Timer Triggered at Timestamp: %ld\n", id, get_current_time());
}

// --- Main 函數 ---

int main(int argc, char *argv[]) {
    // 1. 建立 Pipe
    if (pipe(timer_pipe) == -1) {
        perror("pipe creation failed");
        return 1;
    }

    // 2. 註冊 SIGALRM 信號處理函數 (TISR)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_service_routine; // 註冊 TISR
    sa.sa_flags = SA_RESTART; // 確保系統呼叫(如 read)可重新啟動
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction failed");
        return 1;
    }
    
    // 3. 啟動硬體定時器 (ITIMER_REAL, 10ms Tick)
    struct itimerval timer_spec;
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_usec = TICK_MS * 1000; // 10ms
    timer_spec.it_interval = timer_spec.it_value;

    if (setitimer(ITIMER_REAL, &timer_spec, NULL) == -1) {
        perror("setitimer failed");
        return 1;
    }
    printf("Hardware Timer (ITIMER_REAL) Activated, Tick: %dms.\n", TICK_MS);
    printf("Worker Process ID: %d\n", getpid());
    
    st_start(300, 1, callback_sleep, (void*)1); 
    
    st_start(500, 0, callback_sleep, (void*)2); 
    
    st_start(100, 1, callback_quick, (void*)3);
    
    worker_loop(); 

    // 清理資源
    close(timer_pipe[0]);
    close(timer_pipe[1]);

    return 0;
}