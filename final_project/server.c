#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>

// 與 client.c 中的定義保持一致
#define MAX_INDEXES 100
#define USER_ID_OFFSET 1000 // 僅用於日誌，實際索引使用模除

// ------------------------------------------------
// Structure Definitions (Must Match Client)
// ------------------------------------------------

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
    int ac[MAX_INDEXES];      // 0: In progress, 1: Completed, 99: Not scheduled
    int dry_m[MAX_INDEXES];   
    int wash_m[MAX_INDEXES];  
} device_state;

// --- Global State Simulation ---
device_state g_device_state;
bool g_is_task_scheduled[MAX_INDEXES] = {false}; 

// --- Function Prototypes ---
void handle_client_connection(int client_socket);
void initialize_state();
void display_task_msg(const TaskMsg *msg);
void simulate_task_start(const TaskMsg *msg);
void simulate_task_completion(int index);

// **********************************************
// ************ Simulation Logic ************
// **********************************************

void initialize_state() {
    printf("Initializing server state...\n");
    memset(&g_device_state, 0, sizeof(device_state));
    
    // 預設將所有任務標記為 99 (未排程)
    for (int i = 0; i < MAX_INDEXES; i++) {
        g_device_state.wash_m[i] = 99;
        g_device_state.dry_m[i] = 99;
        g_device_state.ac[i] = 99;
        g_is_task_scheduled[i] = false;
    }
}

void display_task_msg(const TaskMsg *msg) {
    char time_str[32];
    if (msg->original_target_time > 0) {
        struct tm *lt = localtime(&msg->original_target_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", lt);
    } else {
        strcpy(time_str, "N/A (Setup/Request)");
    }
    
    printf("\n--- Received TaskMsg ---\n");
    printf("User ID: %d, Room ID: %d\n", msg->user_id, msg->room_id);
    printf("Target Time: %s\n", time_str);
    printf("Task Modes: WASH=%d, DRY=%d, AC=%d\n", 
           (int)msg->task_mode.MODE_WASH, (int)msg->task_mode.MODE_DRY, (int)msg->task_mode.MODE_AC);
    printf("------------------------\n");
}

void simulate_task_start(const TaskMsg *msg) {
    // 修正點：使用模除計算索引
    int index = msg->user_id % 100;
    
    if (index < 0 || index >= MAX_INDEXES) return; 
    
    if (msg->original_target_time > 0) { 
        printf("SIMULATION: Task for User %d is now IN PROGRESS. (Index: %d)\n", msg->user_id, index);
        g_is_task_scheduled[index] = true;
        
        // 模擬將排程的任務狀態設為「進行中」(0)
        if (msg->task_mode.MODE_WASH) g_device_state.wash_m[index] = 0;
        if (msg->task_mode.MODE_DRY) g_device_state.dry_m[index] = 0;
        if (msg->task_mode.MODE_AC) g_device_state.ac[index] = 0;
    } 
}

void simulate_task_completion(int index) {
    // 30% 機率將進行中的任務標記為完成 (1)
    if (index < 0 || index >= MAX_INDEXES || !g_is_task_scheduled[index]) return; 
    
    // 隨機判斷是否完成
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
    
    // 如果所有排程的任務都已完成，則將 g_is_task_scheduled 設為 false (可選)
}

// **********************************************
// ************ Communication Handler ************
// **********************************************

void handle_client_connection(int client_socket) {
    TaskMsg received_msg;
    // 接收數據
    long valread = recv(client_socket, &received_msg, sizeof(TaskMsg), 0);
    
    if (valread == sizeof(TaskMsg)) {
        display_task_msg(&received_msg);
        
        // 修正點：使用模除 100 計算索引 (與客戶端同步)
        int index = received_msg.user_id % 100;

        if (index < 0 || index >= MAX_INDEXES) {
            printf("Error: User ID %d is out of valid index range (0-99). Cannot process.\n", received_msg.user_id);
            return;
        }

        if (received_msg.original_target_time > 0) {
            // --- 情況 A: 收到 Task Data (Client 選項 3: Send Task Data) ---
            printf("ACTION: Task Data received. Storing new schedule and setting state to IN PROGRESS.\n");
            simulate_task_start(&received_msg);
            
        } else if (received_msg.user_id != 0) {
            // --- 情況 B: 收到狀態請求 (Client 選項 1: Display Device Status) ---
            printf("ACTION: State request received (User %d). Checking for random completion...\n", received_msg.user_id);
            
            // 模擬隨機任務完成
            simulate_task_completion(index);
            
            // 回傳當前的模擬狀態
            send(client_socket, &g_device_state, sizeof(device_state), 0);
            printf("ACTION: Sent device_state back to client. (Index: %d)\n", index);
        }
        
    } else if (valread > 0) {
        printf("Received unknown data of size: %ld\n", valread);
    } else if (valread == 0) {
        printf("Client disconnected gracefully.\n");
    } else {
        perror("Receive failed");
    }
}

// **********************************************
// ************ Server Setup ************
// **********************************************

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int port;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port Number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[1]);

    srand(time(NULL));
    initialize_state();

    // 創建 Socket 檔案描述符
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // 確保可以重複使用地址和端口
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // 綁定到 Port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // 監聽連線
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);
    printf("Array index = User ID MOD 100.\n");

    while (1) {
        // 接受新連線
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }
        printf("\nConnection established from %s.\n", inet_ntoa(address.sin_addr));
        
        // 處理客戶端連線
        handle_client_connection(new_socket);
        
        // 關閉客戶端連線
        close(new_socket);
    }

    close(server_fd);
    return 0;
}