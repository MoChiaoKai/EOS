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
#define JSON_BUFFER_SIZE 1024 // Unified buffer size for main JSON data

// --- Structure Definitions ---

// Device Type Enumeration
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

typedef struct {
    char dorm_location[100]; // Dormitory location/ID
    Device paired_devices[5]; // Paired device list (Max 5)
    int device_count;
} UserData;

// --- Fixed Available Devices List ---
Device g_available_devices[] = {
    // Device Name, Status, Remaining Time, Icon, Type
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
int find_dorm_data(const char *location);
void initial_setup(UserData *user);
bool setup_interface(UserData *user, const char *ip, int port);
void run_main_interface(UserData *user, const char *ip, int port);
void display_device_status(UserData *user);    
void set_estimated_time(UserData *user);        
void device_remote_control(UserData *user);    
void package_data_to_json(UserData *user, char *buffer, size_t buffer_size);
bool send_data_to_server(const char *ip, int port, const char *data_to_send);


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
    printf("Loaded %d dormitory records from %s.\n", g_db_size, DB_FILENAME);
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

int find_dorm_data(const char *location) {
    for (int i = 0; i < g_db_size; i++) {
        if (strcmp(g_dorm_db[i].dorm_location, location) == 0) {
            return i;
        }
    }
    return -1;
}

void initial_setup(UserData *user) {
    printf("\n--- Executing First-Time Setup (Initialization) ---\n");
    
    // 1. Enter Dormitory Location
    printf("1. Please enter your dormitory location (as system identifier): ");
    fgets(user->dorm_location, sizeof(user->dorm_location), stdin);
    user->dorm_location[strcspn(user->dorm_location, "\n")] = 0;
    
    // 2. Select Paired Devices
    printf("\n2. Please select devices to pair (max 5):\n");
    
    // Display list without icons
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
            // Display confirmation without icon
            printf("Paired: %s\n", user->paired_devices[user->device_count].name);
            user->device_count++;
        } else if (index != 0) {
            printf("Invalid selection number: %s\n", token);
        }
        
        token = strtok(NULL, " ,\n"); 
    }
    
    printf("--- Initialization complete. Total paired devices: %d ---\n", user->device_count);
}

bool setup_interface(UserData *user, const char *ip, int port) { 
    int choice;
    char input_location[100];
    int db_index;
    // Client setup uses the unified buffer size
    char json_buffer[JSON_BUFFER_SIZE]; 

    while (true) {
        printf("\n==================================\n");
        printf("System Startup\n"); 
        printf("Is this your first time using the system?\n");
        printf("1. Yes (Proceed to initial setup)\n");
        printf("2. No (Load existing dormitory data)\n");
        printf("0. Exit Program\n");
        printf("Please select: ");

        choice = get_int_input(); 

        if (choice == 1) {
            if (g_db_size >= MAX_DORMS) {
                printf("Database is full. Cannot add new dormitories.\n"); 
                continue;
            }
            
            initial_setup(user);
            
            // Persistence and Socket transmission logic
            g_dorm_db[g_db_size] = *user;
            g_db_size++;
            save_db();

            printf("\n--- STEP: Sending initial setup data to server (Overwrite) ---\n"); 
            package_data_to_json(user, json_buffer, sizeof(json_buffer));
            send_data_to_server(ip, port, json_buffer);
            
            return true;
            
        } else if (choice == 2) {
            printf("Please enter your dormitory location (e.g., North Dorm 305) for verification: ");
            fgets(input_location, sizeof(input_location), stdin);
            input_location[strcspn(input_location, "\n")] = 0;

            db_index = find_dorm_data(input_location);

            if (db_index != -1) {
                *user = g_dorm_db[db_index];
                printf("Dormitory data found! Loaded settings for **%s**.\n", user->dorm_location); 
                return true;
            } else {
                printf("No dormitory data found for (%s). Please check your input or select first-time setup.\n", input_location); 
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
    printf("\nDevice Status Monitoring (%s)\n", user->dorm_location); 
    printf("------------------------------------\n");
    
    if (user->device_count == 0) {
        printf("No paired devices to display.\n");
        return;
    }

    for (int i = 0; i < user->device_count; i++) {
        Device *dev = &user->paired_devices[i];
        const char *status = dev->is_on ? "ON" : "OFF";
        
        // Removed icon from device status display
        printf("%-15s Status: %-5s Remaining Schedule Time: %d minutes\n", 
               dev->name, status, dev->remaining_time);
    }
    printf("------------------------------------\n");
}

// Feature 3: Set Estimated Time of Arrival
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

// Feature 4: Device Remote Control
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
            printf("%s remotely turned OFF.\n", dev->name); 
        } else {
            printf("Invalid action selected.\n"); 
        }
    } else {
        printf("Invalid device number.\n"); 
    }
}

/**
 * @brief Packages all paired device data into a single JSON object with a device array.
 * @param user Pointer to the UserData structure.
 * @param buffer Output buffer to store the resulting JSON string.
 * @param buffer_size The maximum size of the output buffer.
 */
void package_data_to_json(UserData *user, char *buffer, size_t buffer_size) {
    // FIX: Use a larger internal buffer size for device array construction
    char devices_array[JSON_BUFFER_SIZE / 2] = ""; 
    size_t current_len = 0;
    int written;

    if (user->device_count == 0) {
        snprintf(buffer, buffer_size, 
                 "{\"location\":\"%s\", \"message\":\"No devices paired\"}",
                 user->dorm_location);
        return;
    }

    // 1. Build the inner devices array string
    for (int i = 0; i < user->device_count; i++) {
        Device *dev = &user->paired_devices[i];
        
        // Ensure we don't overflow the devices_array buffer. Leave some space (e.g., 50 bytes)
        if (current_len >= sizeof(devices_array) - 50) { 
             printf("Warning: JSON device array buffer near overflow, stopping.\n");
             break;
        }

        // Format a single device object
        written = snprintf(devices_array + current_len, sizeof(devices_array) - current_len,
                           "%s{\"name\":\"%s\", \"status\":%s, \"remaining_time\":%d}",
                           (i == 0) ? "" : ",", // Prepend comma if not the first element
                           dev->name, 
                           dev->is_on ? "true" : "false",
                           dev->remaining_time);

        if (written > 0) {
            current_len += written;
        }
    }

    // 2. Wrap the device array into the final structure
    // This uses the main buffer (passed in `buffer`)
    snprintf(buffer, buffer_size, 
             "{\"location\":\"%s\", \"devices\":[%s]}",
             user->dorm_location, 
             devices_array);
}

bool send_data_to_server(const char *ip, int port, const char *data_to_send) {
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
    
    long valread = send(sock, data_to_send, strlen(data_to_send), 0);
    
    if (valread < 0) {
        perror("Data sending failed");
        close(sock);
        return false;
    }
    
    printf("    > Data sent successfully (%ld bytes).\n", valread);
    
    close(sock);
    return true;
}


// ------------------------------------------------
// Main Interface Loop
// ------------------------------------------------

void run_main_interface(UserData *user, const char *ip, int port) {
    int choice;
    // FIX: Use the larger unified buffer size
    char json_buffer[JSON_BUFFER_SIZE]; 

    do {
        /*
         * Main menu title block removed to simplify output
         */
        
        printf("1. Display Device Status\n");
        printf("2. Set Estimated Time of Arrival\n");
        printf("3. Remote Control Device\n");
        printf("4. Send Device Data to Server\n"); 
        printf("0. Exit Main Menu and return to Startup Interface\n"); 
        printf("Please select a function (0-4): ");
        
        choice = get_int_input();
        
        // Find current user's index in the global database
        int index = find_dorm_data(user->dorm_location);
        
        // Data modifying actions (2, 3) should update the DB and save to file
        if (choice == 2 || choice == 3) { 
            if (index != -1) {
                // Update the global array with modified data
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
                package_data_to_json(user, json_buffer, sizeof(json_buffer));
                send_data_to_server(ip, port, json_buffer);
                break;
            case 0:
                // --- Logic: Clear scheduled time and save upon exit ---
                printf("Exiting Main Menu, returning to the Startup Interface.\n"); 
                
                // 1. Reset remaining time for all devices
                for (int i = 0; i < user->device_count; i++) {
                    user->paired_devices[i].remaining_time = 0;
                }
                
                // 2. Write the reset data back to the global array and save permanently
                if (index != -1) {
                    g_dorm_db[index] = *user;
                    save_db(); 
                    printf("Note: All scheduled times have been reset to 0 and saved.\n");
                }
                // --- Logic End ---
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
        // Pass IP/Port to setup_interface for immediate Socket transmission
        if (setup_interface(&currentUser, server_ip, server_port)) { 
            run_main_interface(&currentUser, server_ip, server_port);
        } else {
            running = false;
        }
    }

    return 0;
}