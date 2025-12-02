#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DB_FILENAME "dorm_db.dat" 
#define MAX_DORMS 10              
#define JSON_BUFFER_SIZE 1024 

// --- Structure Definitions ---

// 🚨 移除 TaskMode Enumeration 🚨

// 🚨 修改後的 Task Structure (只包含 7 個 int 欄位) 🚨
typedef struct {
    int user_id;
    int arrival_time;        // Effective Arrival Time 
    int original_arrival_time; // User's input ETA
    int room_id;
    
    int duration;            
    int start_threshold;     
    
    int status;              
} Task;

// Device Type Enumeration (Unchanged)
typedef enum {
    DEV_WASHER = 0,   // Washer
    DEV_DRYER  = 1,   // Dryer
    DEV_AC     = 2    // Air Conditioner
} DeviceType;

typedef struct {
    char name[32];      
    bool is_on;         
    int remaining_time; // Remaining time for device scheduling (minutes)
    char icon[8];       // Device icon (Retained for data integrity)
    DeviceType type;    // Device type identifier
} Device;

// UserData Structure (Used for persistence/local state)
typedef struct {
    int user_id;  // Used as unique identifier for persistence
    int room_id;  

    Device paired_devices[5]; // Paired device list (Max 5)
    int device_count;
} UserData;

// --- Fixed Available Devices List (Unchanged) ---
Device g_available_devices[] = {
    {"Washer", false, 0, "🧺", DEV_WASHER}, 
    {"Dryer", false, 0, "🔥", DEV_DRYER},
    {"Air Conditioner", false, 0, "❄️", DEV_AC}
};
#define MAX_AVAILABLE_DEVICES (sizeof(g_available_devices) / sizeof(Device))

// --- Global Database (Dynamic Database) ---
UserData g_dorm_db[MAX_DORMS]; 
int g_db_size = 0;             

void clear_input_buffer();
int get_int_input();
void notify_user(const char *task_name);
void schedule_task(const char *task_name, int duration_sec);

// --- Persistence Function Prototypes ---
void load_db();
void save_db();

// --- Core Logic & Feature Function Prototypes ---
int find_user_data(int user_id); 
void initial_setup(UserData *user);
bool setup_interface(UserData *user, const char *ip, int port);
void run_main_interface(UserData *user, const char *ip, int port);
void display_device_status(UserData *user);    
void set_estimated_time(UserData *user);        
void device_remote_control(UserData *user);    

void prepare_task_struct(UserData *user, Task *task_out);
bool send_data_to_server(const char *ip, int port, const void *data_to_send, size_t data_size);


// **********************************************
// ************ Utility and Persistence ************
// **********************************************

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF); 
}

int get_int_input() {
    int val;
    if (scanf("%d", &val) != 1) {
        clear_input_buffer(); 
        return -1; 
    }
    clear_input_buffer();
    return val;
}

void notify_user(const char *task_name) {
    printf("\n[NOTIFICATION]: Your task %s has been completed.\n", task_name);
}

void schedule_task(const char *task_name, int duration_sec) {
    printf("\nSystem scheduling based on mode: Task %s...\n", task_name); 
    
    for (int i = 1; i <= 3; i++) {
        sleep(1); 
        printf("    > In progress... (%d%%)\n", i * 33);
    }
    
    printf("Task %s finished!\n", task_name); 
    notify_user(task_name);
}

void load_db() {
    FILE *file = fopen(DB_FILENAME, "rb");
    if (file == NULL) {
        printf("Database file not found or empty. Starting with fresh database.\n");
        g_db_size = 0;
        return;
    }

    g_db_size = 0;
    while (g_db_size < MAX_DORMS && fread(&g_dorm_db[g_db_size], sizeof(UserData), 1, file) == 1) {
        g_db_size++;
    }
    
    fclose(file);
    printf("Loaded %d user records from %s.\n", g_db_size, DB_FILENAME);
}

void save_db() {
    FILE *file = fopen(DB_FILENAME, "wb"); 
    if (file == NULL) {
        perror("Error opening database file for writing");
        return;
    }

    size_t written = fwrite(g_dorm_db, sizeof(UserData), g_db_size, file);
    
    if (written == g_db_size) {
        printf("Database saved successfully (%d records).\n", g_db_size);
    } else {
        printf("Error saving database. Only wrote %zu records out of %d.\n", written, g_db_size);
    }

    fclose(file);
}


// **********************************************
// ************ Flow Control and Features ************
// **********************************************

int find_user_data(int user_id) {
    for (int i = 0; i < g_db_size; i++) {
        if (g_dorm_db[i].user_id == user_id) {
            return i;
        }
    }
    return -1;
}

void initial_setup(UserData *user) {
    printf("\n--- Executing First-Time Setup (Initialization) ---\n");
    
    // 1. Enter User ID
    printf("1. Please enter your User ID: ");
    user->user_id = get_int_input();
    if (user->user_id <= 0) {
        printf("Invalid ID, setting to default 9999.\n");
        user->user_id = 9999;
    }

    // 2. Enter Room ID
    printf("2. Please enter your Room ID: ");
    user->room_id = get_int_input();
    if (user->room_id <= 0) {
        printf("Invalid Room ID, setting to default 101.\n");
        user->room_id = 101;
    }
    
    // 3. Select Paired Devices
    printf("\n3. Please select devices to pair (max 5):\n");
    
    for (int i = 0; i < MAX_AVAILABLE_DEVICES; i++) {
        printf("%d. %s\n", i + 1, g_available_devices[i].name);
    }
    printf("Enter device numbers separated by space or comma (e.g., 1, 3): ");
    
    char choice_str[32]; 
    fgets(choice_str, sizeof(choice_str), stdin);
    
    char *token = strtok(choice_str, " ,\n"); 
    user->device_count = 0;
    
    printf("--- Pairing selected devices ---\n");
    
    while (token != NULL && user->device_count < 5) {
        int index = atoi(token); 
        
        if (index >= 1 && index <= MAX_AVAILABLE_DEVICES) {
            user->paired_devices[user->device_count] = g_available_devices[index - 1];
            printf("Paired: %s\n", user->paired_devices[user->device_count].name);
            user->device_count++;
        } else if (index != 0) {
            printf("Invalid selection number: %s\n", token);
        }
        
        token = strtok(NULL, " ,\n"); 
    }
    
    printf("--- Initialization complete. Total paired devices: %d ---\n", user->device_count);
    printf("--- User ID: %d, Room ID: %d ---\n", user->user_id, user->room_id);
}

bool setup_interface(UserData *user, const char *ip, int port) { 
    int choice;
    int input_user_id;
    int db_index;
    
    Task task_to_send; 

    while (true) {
        printf("\n==================================\n");
        printf("System Startup\n"); 
        printf("Is this your first time using the system?\n");
        printf("1. Yes (Proceed to initial setup)\n");
        printf("2. No (Load existing user data)\n");
        printf("0. Exit Program\n");
        printf("Please select: ");

        choice = get_int_input(); 

        if (choice == 1) {
            if (g_db_size >= MAX_DORMS) {
                printf("Database is full. Cannot add new users.\n"); 
                continue;
            }
            
            initial_setup(user);
            
            // Persistence and Socket transmission logic
            g_dorm_db[g_db_size] = *user;
            g_db_size++;
            save_db();

            printf("\n--- STEP: Sending initial setup data to server (Task) ---\n"); 
            prepare_task_struct(user, &task_to_send);
            send_data_to_server(ip, port, &task_to_send, sizeof(Task));
            
            return true;
            
        } else if (choice == 2) {
            printf("Please enter your User ID for verification: ");
            input_user_id = get_int_input();

            db_index = find_user_data(input_user_id);

            if (db_index != -1) {
                *user = g_dorm_db[db_index];
                printf("User data found! Loaded settings for User ID: %d, Room ID: %d.\n", 
                       user->user_id, user->room_id); 
                return true;
            } else {
                printf("No user data found for ID (%d). Please check your input or select first-time setup.\n", input_user_id); 
            }

        } else if (choice == 0) {
            printf("Exiting program.\n"); 
            return false;
        } else {
            printf("Invalid input. Please select again.\n"); 
        }
    }
}

// Feature 2: Display Device Status
void display_device_status(UserData *user) {
    printf("\nDevice Status Monitoring (User ID: %d, Room ID: %d)\n", user->user_id, user->room_id); 
    printf("------------------------------------\n");
    
    if (user->device_count == 0) {
        printf("No paired devices to display.\n");
        return;
    }

    for (int i = 0; i < user->device_count; i++) {
        Device *dev = &user->paired_devices[i];
        const char *status = dev->is_on ? "ON" : "OFF";
        
        printf("%-15s Status: %-5s Remaining Schedule Time: %d minutes\n", 
               dev->name, status, dev->remaining_time);
    }
    printf("------------------------------------\n");
}

// Feature 3: Set Estimated Time of Arrival (Unchanged)
void set_estimated_time(UserData *user) {
    printf("\nSetting Estimated Time of Arrival (ETA)\n"); 
    printf("Enter estimated time until arrival (in minutes): ");
    int minutes = get_int_input();

    if (minutes <= 0) {
        printf("Invalid time setting or cancelled.\n"); 
        return;
    }

    printf("Enter device numbers to schedule activation (e.g., 12): ");
    char choice_str[10];
    fgets(choice_str, sizeof(choice_str), stdin);
    
    printf("--- Scheduled activation in %d minutes ---\n", minutes);
    for (int i = 0; i < strlen(choice_str); i++) {
        int index = choice_str[i] - '1';
        if (index >= 0 && index < user->device_count) {
            user->paired_devices[index].remaining_time = minutes;
            printf("%s scheduled for activation, Remaining Time: %d minutes.\n", 
                   user->paired_devices[index].name, minutes);
        }
    }
    notify_user("ETA scheduling complete");
}

// Feature 4: Device Remote Control (Unchanged)
void device_remote_control(UserData *user) {
    printf("\nEntering Remote Control Mode\n"); 
    if (user->device_count == 0) {
        printf("No paired devices to control.\n"); 
        return;
    }

    display_device_status(user);
    
    printf("Enter the number of the device to control: ");
    int index = get_int_input() - 1; 

    if (index >= 0 && index < user->device_count) {
        Device *dev = &user->paired_devices[index];
        printf("Controlling Device: %s (Current Status: %s)\n", dev->name, dev->is_on ? "ON" : "OFF");
        printf("Select action (1: ON / 0: OFF): ");
        
        int action = get_int_input();

        if (action == 1) {
            dev->is_on = true;
            printf("%s remotely turned ON.\n", dev->name); 
        } else if (action == 0) {
            dev->is_on = false;
            // Clear scheduled time when device is turned off
            if (dev->remaining_time > 0) {
                dev->remaining_time = 0;
                printf("[INFO]: Scheduled time for %s reset to 0 minutes.\n", dev->name);
            }
            printf("%s remotely turned OFF.\n", dev->name); 
        } else {
            printf("Invalid action selected.\n"); 
        }
    } else {
        printf("Invalid device number.\n"); 
    }
}

// 🚨 Modified: Prepare Task Struct (Removed mode field) 🚨
void prepare_task_struct(UserData *user, Task *task_out) {
    
    // 1. Get the highest scheduled time to represent original_arrival_time
    int max_remaining_time = 0;
    for(int i = 0; i < user->device_count; i++) {
        if (user->paired_devices[i].remaining_time > max_remaining_time) {
            max_remaining_time = user->paired_devices[i].remaining_time;
        }
    }

    // 2. Initialize the entire Task structure to zero (set all fields to 0)
    memset(task_out, 0, sizeof(Task));
    
    // 3. Fill the required fields
    task_out->user_id = user->user_id;
    task_out->original_arrival_time = max_remaining_time; // Use max scheduled time as ETA
    task_out->room_id = user->room_id;
    
    // Output check (Optional, for debugging)
    printf("[DEBUG] Task struct prepared: ID=%d, Room=%d, ETA=%d\n", 
           task_out->user_id, task_out->room_id, task_out->original_arrival_time);
}

// Send data remains the same (it uses sizeof(Task) dynamically)
bool send_data_to_server(const char *ip, int port, const void *data_to_send, size_t data_size) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    printf("\nAttempting to connect to server %s:%d...\n", ip, port); 

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return false;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); 

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) { 
        printf("Invalid IP Address.\n");
        close(sock);
        return false;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed (Ensure server is running on the correct IP/Port)");
        return false;
    }
    
    printf("    > Connection successful!\n");
    
    // Send raw struct data 
    long valread = send(sock, data_to_send, data_size, 0);
    
    if (valread < 0) {
        perror("Data sending failed");
        close(sock);
        return false;
    }
    
    printf("    > Data sent successfully (%ld bytes, Expected: %zu bytes).\n", valread, data_size);
    
    close(sock);
    return true;
}


// ------------------------------------------------
// Main Interface Loop
// ------------------------------------------------

void run_main_interface(UserData *user, const char *ip, int port) {
    int choice;
    Task task_to_send; 

    do {
        printf("\n==================================\n");
        printf("Smart Dormitory System Main Menu\n"); 
        printf("    User: %d | Room: %d\n", user->user_id, user->room_id);
        printf("==================================\n");

        printf("1. Display Device Status\n");
        printf("2. Set Estimated Time of Arrival\n");
        printf("3. Remote Control Device\n");
        printf("4. Send Task Data to Server (Binary Struct)\n"); 
        printf("0. Exit Main Menu and return to Startup Interface\n"); 
        printf("Please select a function (0-4): ");
        
        choice = get_int_input();
        
        // Find current user's index in the global database
        int index = find_user_data(user->user_id);
        
        // Data modifying actions (2, 3) should update the DB and save to file
        if (choice == 2 || choice == 3) { 
            if (index != -1) {
                g_dorm_db[index] = *user;
                save_db(); // Save database
            }
        }
        
        switch (choice) {
            case 1: 
                display_device_status(user);
                break;
            case 2: 
                set_estimated_time(user);
                break;
            case 3: 
                device_remote_control(user);
                break;
            case 4: 
                prepare_task_struct(user, &task_to_send);
                send_data_to_server(ip, port, &task_to_send, sizeof(Task));
                break;
            case 0:
                // Logic: Clear scheduled time and save upon exit
                printf("Exiting Main Menu, returning to the Startup Interface.\n"); 
                
                // Reset remaining time for all devices
                for (int i = 0; i < user->device_count; i++) {
                    user->paired_devices[i].remaining_time = 0;
                }
                
                // Write the reset data back to the global array and save permanently
                if (index != -1) {
                    g_dorm_db[index] = *user;
                    save_db(); 
                    printf("Note: All scheduled times have been reset to 0 and saved.\n");
                }
                break;
            default:
                printf("Invalid selection. Please try again.\n"); 
        }
    } while (choice != 0);
}


// ------------------------------------------------
// Main Program Entry Point
// ------------------------------------------------

int main(int argc, char *argv[]) {
    
    // 1. Command Line Argument Check (IP & Port)
    if (argc != 3) {
        printf("Usage Error! Please provide the server's IP address and port number.\n");
        printf("Example: ./client <IP Address> <Port Number>\n");
        return 1; 
    }
    const char *server_ip = argv[1]; 
    int server_port = atoi(argv[2]); 

    if (server_port <= 0) {
        printf("Error: Port number must be a positive integer.\n");
        return 1;
    }

    // 2. Load the database from file at startup
    load_db();

    UserData currentUser;
    bool running = true;

    // 3. System Main Loop
    while(running) {
        if (setup_interface(&currentUser, server_ip, server_port)) { 
            run_main_interface(&currentUser, server_ip, server_port);
        } else {
            running = false;
        }
    }

    return 0;
}