#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/wait.h> 

#define MAX_INDEXES 100

typedef struct {
    bool MODE_WASH;
    bool MODE_DRY;
    bool MODE_AC;
} TaskMode;

typedef struct {
    int user_id;
    int room_id;
    time_t original_target_time;
    TaskMode task_mode;
} TaskMsg;

typedef struct {
    int ac[MAX_INDEXES];    
    int dry_m[MAX_INDEXES];  
    int wash_m[MAX_INDEXES]; 
} device_state;

device_state g_device_state;

// --- Function Prototypes ---
void handle_client_long_connection(int client_socket, const char *client_ip);
void initialize_state();
void display_task_msg(const TaskMsg *msg);
void simulate_task_start(const TaskMsg *msg);
void simulate_task_completion_and_send(int client_socket, int index);
void sigchld_handler(int s); 

void initialize_state() {
    printf("Initializing server state...\n");
    memset(&g_device_state, 0, sizeof(device_state));
    for (int i = 0; i < MAX_INDEXES; i++) {
        g_device_state.wash_m[i] = 99;
        g_device_state.dry_m[i] = 99;
        g_device_state.ac[i] = 99;
    }
}

void display_task_msg(const TaskMsg *msg) {
    char time_str[32];
    if (msg->original_target_time > 0) {
        struct tm *lt = localtime(&msg->original_target_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", lt);
    } else {
        strcpy(time_str, "N/A (Status Request)");
    }
    
    printf("\n--- Received TaskMsg ---\n");
    printf("User ID: %d, Room ID: %d\n", msg->user_id, msg->room_id);
    printf("Target Time: %s\n", time_str);
    printf("Task Modes: WASH=%d, DRY=%d, AC=%d\n", 
           (int)msg->task_mode.MODE_WASH, (int)msg->task_mode.MODE_DRY, (int)msg->task_mode.MODE_AC);
    printf("------------------------\n");
}

void simulate_task_start(const TaskMsg *msg) {
    int index = msg->user_id % 100;
    
    if (index < 0 || index >= MAX_INDEXES) return; 
    
    printf("SIMULATION: User %d Task Updated. (Index: %d)\n", msg->user_id, index);
    
    g_device_state.wash_m[index] = msg->task_mode.MODE_WASH ? 0 : 99;
    g_device_state.dry_m[index] = msg->task_mode.MODE_DRY ? 0 : 99;
    g_device_state.ac[index] = msg->task_mode.MODE_AC ? 0 : 99;
}

void simulate_task_completion_and_send(int client_socket, int index) {
    
    if (index < 0 || index >= MAX_INDEXES) return; 
    
    if (g_device_state.wash_m[index] == 0 && (rand() % 100 < 30)) {
        g_device_state.wash_m[index] = 1;
        printf("SIMULATION: Washer for Index %d completed.\n", index);
    }
    if (g_device_state.dry_m[index] == 0 && (rand() % 100 < 30)) {
        g_device_state.dry_m[index] = 1;
        printf("SIMULATION: Dryer for Index %d completed.\n", index);
    }
    if (g_device_state.ac[index] == 0 && (rand() % 100 < 30)) {
        g_device_state.ac[index] = 1;
        printf("SIMULATION: AC for Index %d completed.\n", index);
    }

    if (send(client_socket, &g_device_state, sizeof(device_state), 0) < 0) {
        perror("Send state failed");
    } else {
        printf("ACTION: Sent device_state back to client. (Index: %d)\n", index);
    }
}

void handle_client_long_connection(int client_socket, const char *client_ip) {
    TaskMsg received_msg;
    long valread;
    int index;

    while (1) {
        valread = recv(client_socket, &received_msg, sizeof(TaskMsg), MSG_WAITALL);

        if (valread == sizeof(TaskMsg)) {
            display_task_msg(&received_msg);
            
            index = received_msg.user_id % 100;

            if (index < 0 || index >= MAX_INDEXES || received_msg.user_id == 0) {
                printf("Error: Invalid User ID %d received.\n", received_msg.user_id);
                continue;
            }

            if (received_msg.original_target_time > 0 || 
                received_msg.task_mode.MODE_WASH ||
                received_msg.task_mode.MODE_DRY ||
                received_msg.task_mode.MODE_AC) {
                
                simulate_task_start(&received_msg);
                
            } else if (received_msg.user_id != 0) {
                
                printf("ACTION: State request received (User %d). Simulating completion...\n", received_msg.user_id);
                simulate_task_completion_and_send(client_socket, index);
            }

        } else if (valread == 0) {
            printf("Client %s disconnected gracefully.\n", client_ip);
            break;
        } else if (valread < 0) {
            if (errno == ECONNRESET) {
                 printf("Client %s forcibly closed the connection (RST received).\n", client_ip);
            } else {
                 perror("Receive failed");
            }
            break;
        } else {
            printf("Client %s sent incomplete data of size: %ld (Expected: %zu). Closing connection.\n", client_ip, valread, sizeof(TaskMsg));
            break;
        }
    }

    close(client_socket);
    exit(0); 
}

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}


// **********************************************
// ************ Server Setup (Multi-Process) ************
// **********************************************

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int port;
    pid_t child_pid;
    struct sigaction sa;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port Number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[1]);

    srand(time(NULL));
    initialize_state();

    sa.sa_handler = sigchld_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d... (Multi-Process Mode)\n", port);
    printf("Waiting for client connections...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Accept failed");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        
        child_pid = fork();
        
        if (child_pid < 0) {
            perror("Fork failed");
            close(new_socket);
            continue;
        }
        
        if (child_pid == 0) {
            close(server_fd);
            printf("\nChild Process %d: Connection established from %s.\n", getpid(), client_ip);
            
            handle_client_long_connection(new_socket, client_ip);
            
        } else {
            close(new_socket);
        }
    }

    close(server_fd);
    return 0;
}