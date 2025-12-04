#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h> 
#include <time.h>      // For time_t and time conversion

// --- Structure Definitions (MUST MATCH CLIENT'S STRUCTURES EXACTLY) ---

// TaskMode Structure (Contains device boolean states) 
typedef struct {
    bool MODE_WASH; // 洗
    bool MODE_DRY;  // 烘
    bool MODE_AC;   // ac    
} TaskMode;

// TaskMsg Structure (The main structure for binary transfer) 
typedef struct {
    int user_id;
    int room_id;
    time_t original_target_time; // Absolute target time (time_t)
    TaskMode task_mode;        // Embedded device mode struct
} TaskMsg;

// --- Server Implementation ---

#define BACKLOG 10 // Maximum number of pending connections

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int port;

    // 1. Argument Check (Port)
    if (argc != 2) {
        printf("Usage Error! Please provide the port number.\n");
        printf("Example: ./server <Port Number>\n");
        return 1;
    }
    port = atoi(argv[1]);

    if (port <= 0) {
        printf("Error: Port number must be a positive integer.\n");
        return 1;
    }
    
    // --- Server Setup (Standard Socket Operations) ---
    
    // Create Socket File Descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    printf("\n============================================\n");
    printf("--- Simple Binary Task Server Started ---\n");
    printf("Listening on port %d. Waiting for %zu bytes struct...\n", port, sizeof(TaskMsg));
    printf("============================================\n\n");

    // 6. Main Server Loop: Accept and Handle Connections
    while (1) {
        printf("Waiting for a connection...\n");
        
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf(">> Connection accepted from Client: %s:%d\n", client_ip, ntohs(address.sin_port));

        TaskMsg received_msg;
        long bytes_received;
        
        // Receive data directly into the TaskMsg struct 
        bytes_received = recv(new_socket, &received_msg, sizeof(TaskMsg), 0);

        if (bytes_received == sizeof(TaskMsg)) {
            char time_str[64] = "N/A (No Schedule)";
            
            // Convert absolute time_t to readable format if set
            if (received_msg.original_target_time != 0) {
                struct tm *tm_info = localtime(&received_msg.original_target_time);
                strftime(time_str, 64, "%Y-%m-%d %H:%M:%S", tm_info);
            }
            
            printf("\n---------------- RECEIVED TASK STRUCT ----------------\n");
            printf("Bytes Received: %ld (Expected: %zu bytes)\n", bytes_received, sizeof(TaskMsg));
            
            // --- Verification ---
            printf("📝 User ID (user_id): %d\n", received_msg.user_id);
            printf("📝 Room ID (room_id): %d\n", received_msg.room_id);
            printf("📝 Original Target Time: %s (time_t: %ld)\n", time_str, (long)received_msg.original_target_time);
            
            // Show Task Modes
            printf("⚙️ Task Modes:\n"); 
            printf("   > Washer (MODE_WASH): %s\n", received_msg.task_mode.MODE_WASH ? "ON (True)" : "OFF (False)");
            printf("   > Dryer (MODE_DRY): %s\n", received_msg.task_mode.MODE_DRY ? "ON (True)" : "OFF (False)");
            printf("   > AC (MODE_AC): %s\n", received_msg.task_mode.MODE_AC ? "ON (True)" : "OFF (False)");
            printf("-----------------------------------------------------\n");

            // Send a response back to the client
            const char *response_message = "Server received TaskMsg struct (Binary OK).";
            send(new_socket, response_message, strlen(response_message), 0);
            printf(">> Confirmation message sent back to client.\n");
            
        } else if (bytes_received > 0) {
            printf("❌ ERROR: Received %ld bytes, but expected %zu bytes (Struct size mismatch).\n", bytes_received, sizeof(TaskMsg));
        } else if (bytes_received == 0) {
            printf("Client disconnected.\n");
        } else {
            perror("Recv failed");
        }

        // Close the current connection socket
        close(new_socket);
        printf("Connection closed. Waiting for the next request...\n\n");
    }

    close(server_fd);
    return 0;
}