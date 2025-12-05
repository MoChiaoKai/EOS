#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>      // For time_t, time(), struct tm, mktime
#include <math.h>      // For ceil(), difftime

#define DB_FILENAME "dorm_db.dat" 
#define MAX_DORMS 10              
#define JSON_BUFFER_SIZE 1024 

// --- Structure Definitions ---

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

// Device Type Enumeration (Used for local mapping/selection)
typedef enum {
    DEV_WASHER = 0,   // Washer
    DEV_DRYER  = 1,   // Dryer
    DEV_AC     = 2    // Air Conditioner
} DeviceType;

// MODIFIED Device Structure (Removed is_on, icon)
typedef struct {
    char name[32];      
    time_t target_time; // Absolute target time (time_t)
    DeviceType type;    // Device type identifier
} Device;

// UserData Structure (Used for persistence/local state)
typedef struct {
    int user_id;  // Used as unique identifier for persistence
    int room_id;  

    Device paired_devices[5]; // Paired device list (Max 5)
    int device_count;
} UserData;

// --- Fixed Available Devices List (No icons) ---
Device g_available_devices[] = {
    // Note: We initialize target_time to 0.
    {"Washer", 0, DEV_WASHER}, 
    {"Dryer", 0, DEV_DRYER},
    {"Air Conditioner", 0, DEV_AC}
};
#define MAX_AVAILABLE_DEVICES (sizeof(g_available_devices) / sizeof(Device))

// --- Global Database (Dynamic Database) ---
UserData g_dorm_db[MAX_DORMS]; 
int g_db_size = 0;             

void clear_input_buffer();
int get_int_input();
void notify_user(const char *task_name);

// Time processing helper 
time_t input_time_to_seconds(const char *time_str);

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

void prepare_task_struct(UserData *user, TaskMsg *msg_out);
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

time_t input_time_to_seconds(const char *time_str) {
    int hour, minute;
    
    // Check for HH:MM format validity
    if (sscanf(time_str, "%d:%d", &hour, &minute) != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return 0; // Invalid input
    }

    time_t now = time(NULL);
    struct tm *today = localtime(&now);
    
    // Set tm struct fields for the target time today
    today->tm_hour = hour;
    today->tm_min = minute;
    today->tm_sec = 0;
    
    // Convert back to time_t (seconds since epoch)
    time_t target_time = mktime(today);

    // If target time is in the past (before 'now'), set it for tomorrow
    if (target_time < now) {
        target_time += 24 * 60 * 60; // Add 24 hours
    }

    return target_time;
}

void notify_user(const char *task_name) {
    //printf("\nYour task %s has been completed.\n", task_name);
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
    //printf("Loaded %d user records from %s.\n", g_db_size, DB_FILENAME);
}

void save_db() {
    FILE *file = fopen(DB_FILENAME, "wb"); 
    if (file == NULL) {
        perror("Error opening database file for writing\n");
        return;
    }

    size_t written = fwrite(g_dorm_db, sizeof(UserData), g_db_size, file);
    
    if (written == g_db_size) {
        //printf("Database saved successfully (%d records).\n", g_db_size);
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
    printf("\n--- Sign up ---\n");
    
    // 1. Enter User ID
    printf("\n1. Please enter your User ID: ");
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
    printf("3. Please select devices to control :\n");
    
    for (int i = 0; i < MAX_AVAILABLE_DEVICES; i++) {
        printf("%d. %s\n", i + 1, g_available_devices[i].name);
    }
    printf("Enter device numbers separated by space or comma (e.g., 1, 3): ");
    
    char choice_str[32]; 
    fgets(choice_str, sizeof(choice_str), stdin);
    
    char *token = strtok(choice_str, " ,\n"); 
    user->device_count = 0;
    
    printf("\n--- Selected devices ---\n");
    
    while (token != NULL && user->device_count < 5) {
        int index = atoi(token); 
        
        if (index >= 1 && index <= MAX_AVAILABLE_DEVICES) {
            user->paired_devices[user->device_count] = g_available_devices[index - 1];
            printf("Controlled: %s\n", user->paired_devices[user->device_count].name); 
            user->device_count++;
        } else if (index != 0) {
            printf("Invalid selection number: %s\n", token);
        }
        
        token = strtok(NULL, " ,\n"); 
    }
    
    printf("--- User ID: %d, Room ID: %d ---\n", user->user_id, user->room_id);
}

bool setup_interface(UserData *user, const char *ip, int port) { 
    int choice;
    int input_user_id;
    int db_index;
    
    TaskMsg task_to_send; 

    while (true) {
        //printf("\n==================================\n");
        // printf("System Startup\n"); 
        printf("Is this your first time using the system?\n");
        printf("1. Yes (sign up)\n");
        printf("2. No (Load user data)\n");
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

            // printf("\n--- STEP: Sending initial setup data to server (Task) ---\n"); 
            prepare_task_struct(user, &task_to_send);
            send_data_to_server(ip, port, &task_to_send, sizeof(TaskMsg));
            
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
                printf("No user data found for ID (%d). Please check your ID or select sign up.\n", input_user_id); 
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
    time_t now = time(NULL);
    struct tm *tm_info;
    char target_time_str[64];
    
    printf("\nDevice Status displaying (User ID: %d, Room ID: %d)\n", user->user_id, user->room_id); 
    printf("------------------------------------\n");
    
    if (user->device_count == 0) {
        printf("No devices to display.\n");
        return;
    }

    for (int i = 0; i < user->device_count; i++) {
        Device *dev = &user->paired_devices[i];

        const char *status = (dev->target_time > 0) ? "ON" : "OFF"; 
        
        // --- Time Display Logic ---
        if (dev->target_time > 0) {
            // Calculate time difference in seconds
            double diff_sec = difftime(dev->target_time, now);
            
            tm_info = localtime(&dev->target_time);
            // Time format without seconds
            strftime(target_time_str, 64, "%H:%M", tm_info); 

            if (diff_sec > 0) {
                // Task is pending, Status is ON (as per user request)
                long diff_min = (long)ceil(diff_sec / 60.0);
                
                // Corrected status display for absolute time 
                printf("%-15s Status: %-5s Target Time: %s (Remaining: %ld minutes)\n", 
                       dev->name, status, target_time_str, diff_min);
            } else {
                // Target time reached or passed
                // Status remains ON temporarily for display, but schedule is cleared below
                
                // Reset target_time to 0, fulfilling the 'preserve until time is reached' request
                dev->target_time = 0; 
                
                printf("%-15s Status: %-5s Target Time: %s (STATUS: PASSED)\n", 
                       dev->name, status, target_time_str);
            }
        } else {
            // No time scheduled
            printf("%-15s Status: %-5s Target Time: Not Scheduled\n", 
                   dev->name, status);
        }
        // --- End Time Display Logic ---
    }
    printf("------------------------------------\n");
}

// Feature 3: Set Estimated Time of Arrival
void set_estimated_time(UserData *user) {
    // Increased buffer size for robustness
    char time_str[10]; 
    time_t future_time;
    struct tm *tm_info;
    char future_time_str[64];
    
    printf("\nSetting Estimated Time of Arrival\n"); 
    printf("Enter estimated arrival time (e.g., 17:30): ");
    
    // Read the time string
    if (fgets(time_str, sizeof(time_str), stdin) == NULL) {
        printf("Input error or cancelled.\n");
        return;
    }
    // Remove newline
    time_str[strcspn(time_str, "\n")] = 0; 

    // Check if input is empty
    if (time_str[0] == '\0') {
        printf("Invalid time input or cancelled.\n");
        return;
    }

    // Convert HH:MM to time_t
    future_time = input_time_to_seconds(time_str);

    if (future_time == 0) {
        printf("Invalid time format or value.\n"); 
        return;
    }
    
    tm_info = localtime(&future_time);
    // Time format without seconds
    strftime(future_time_str, 64, "%H:%M", tm_info); 

    // FIX: Now the program correctly waits for device number input
    printf("Enter device numbers to schedule activation (e.g., 1 2): ");
    char choice_str[10];
    if (fgets(choice_str, sizeof(choice_str), stdin) == NULL) {
        printf("Device selection input error.\n");
        return;
    }
    choice_str[strcspn(choice_str, "\n")] = 0; // Remove newline
    
    // Check if device selection was empty
    if (choice_str[0] == '\0') {
        printf("No devices selected. Scheduling cancelled.\n");
        return;
    }

    printf("--- Scheduled activation at %s ---\n", future_time_str);
    
    for (int i = 0; i < strlen(choice_str); i++) {
        int index = choice_str[i] - '1';
        if (index >= 0 && index < user->device_count) {
            // Store the absolute future time
            user->paired_devices[index].target_time = future_time;
            printf("%s scheduled, Target Time: %s.\n", 
                   user->paired_devices[index].name, future_time_str);
        }
    }
}

/**
 * @brief Prepares and initializes the TaskMsg structure for binary transmission.
 */
void prepare_task_struct(UserData *user, TaskMsg *msg_out) {
    
    // 1. Get the latest target time scheduled among all devices
    time_t latest_target_time = 0;
    // Determine the current device ON/OFF status (ON if target_time > 0)
    bool wash_on = false;
    bool dry_on = false;
    bool ac_on = false;
    
    for(int i = 0; i < user->device_count; i++) {
        Device *dev = &user->paired_devices[i];

        if (dev->target_time > latest_target_time) {
            latest_target_time = dev->target_time;
        }
        
        // If target_time is set, assume the device is "scheduled ON"
        if (dev->target_time > 0) {
            switch (dev->type) {
                case DEV_WASHER:
                    wash_on = true;
                    break;
                case DEV_DRYER:
                    dry_on = true;
                    break;
                case DEV_AC:
                    ac_on = true;
                    break;
            }
        }
    }

    // 2. Initialize the entire TaskMsg structure to zero 
    memset(msg_out, 0, sizeof(TaskMsg));
    
    // 3. Fill the required fields
    msg_out->user_id = user->user_id;
    msg_out->room_id = user->room_id;
    // Send the absolute time_t value
    msg_out->original_target_time = latest_target_time; 
    
    // 4. Fill the TaskMode sub-struct
    msg_out->task_mode.MODE_WASH = wash_on;
    msg_out->task_mode.MODE_DRY = dry_on;
    msg_out->task_mode.MODE_AC = ac_on;
    
    // Output check (Optional, for debugging)
    /*printf("[DEBUG] TaskMsg struct prepared: ID=%d, Room=%d, Target Time=%ld\n", 
           msg_out->user_id, msg_out->room_id, (long)msg_out->original_target_time);*/
}

bool send_data_to_server(const char *ip, int port, const void *data_to_send, size_t data_size) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    // printf("\nAttempting to connect to server %s:%d...\n", ip, port); 

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
    
    // printf("    > Connection successful!\n");
    
    // Send raw struct data 
    long valread = send(sock, data_to_send, data_size, 0);
    
    if (valread < 0) {
        perror("Data sending failed");
        close(sock);
        return false;
    }
    
    // printf("    > Data sent successfully (%ld bytes, Expected: %zu bytes).\n", valread, data_size);
    
    close(sock);
    return true;
}


// ------------------------------------------------
// Main Interface Loop
// ------------------------------------------------

void run_main_interface(UserData *user, const char *ip, int port) {
    int choice;
    TaskMsg task_to_send; 
    
    // Time display variables
    time_t now;
    struct tm *tm_info;
    char time_buffer[64];

    do {
        // --- Display Clock ---
        now = time(NULL);
        tm_info = localtime(&now);
        // Time format without seconds
        strftime(time_buffer, 64, "%H:%M", tm_info); 

        printf("\n==================================\n");
        printf("Smart Dormitory System Main Menu\n"); 
        printf("    [Current Time: %s]\n", time_buffer);
        printf("    User: %d | Room: %d\n", user->user_id, user->room_id);
        printf("==================================\n");
        // --- End Display Clock ---


        printf("1. Display Device Status\n");
        printf("2. Set Estimated Time of Arrival\n"); 
        printf("3. Send Task Data to Server \n"); 
        printf("0. Exit Main Menu and return to Startup Interface\n"); 
        printf("Please select a function (0-3): "); 
        
        choice = get_int_input();
        
        // Find current user's index in the global database
        int index = find_user_data(user->user_id);
        
        // Data modifying action (2) should update the DB and save to file
        if (choice == 2) { 
            if (index != -1) {
                // Save DB after setting ETA (which modifies user data)
                g_dorm_db[index] = *user;
                save_db(); 
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
                prepare_task_struct(user, &task_to_send);
                send_data_to_server(ip, port, &task_to_send, sizeof(TaskMsg));
                break;
            case 0:
                // Logic: Clear scheduled time and save upon exit
                printf("Exiting Main Menu, returning to the Startup Interface.\n"); 
                
                // Write the reset data back to the global array and save permanently
                if (index != -1) {
                    g_dorm_db[index] = *user;
                    save_db(); 
                    printf("Note: All scheduled target times have been reset and saved.\n");
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