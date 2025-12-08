#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include <termios.h>
#include <fcntl.h>

#define DB_FILENAME "dorm_db.dat"
#define MAX_DORMS 100

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

typedef enum {
    DEV_WASHER = 0,
    DEV_DRYER = 1,
    DEV_AC = 2
} DeviceType;

typedef struct {
    char name[32];
    time_t target_time;
    DeviceType type;
} Device;

typedef struct {
    int user_id;
    int room_id;

    Device control_devices[5];
    int device_count;
    bool task_wash_done;
    bool task_dry_done;
    bool task_ac_done;
} UserData;

typedef struct {
    int ac[MAX_DORMS];    
    int dry_m[MAX_DORMS];  
    int wash_m[MAX_DORMS]; 
} device_state;

Device g_available_devices[] = {
    {"Washer", 0, DEV_WASHER},
    {"Dryer", 0, DEV_DRYER},
    {"Air Conditioner", 0, DEV_AC}};
#define MAX_AVAILABLE_DEVICES (sizeof(g_available_devices) / sizeof(Device))

// --- Global Database (Dynamic Database) ---
UserData g_dorm_db[MAX_DORMS];
int g_db_size = 0;

// --- Function Prototypes ---
void clear_input_buffer();
int get_int_input();
time_t input_time_to_seconds(const char *time_str);
void load_db();
void save_db();
int find_user_data(int user_id);
void initial_setup(UserData *user);
bool setup_interface(UserData *user, const char *ip, int port);
void run_main_interface(UserData *user, const char *ip, int port);
void display_device_status(UserData *user, const char *ip, int port);
void set_estimated_time(UserData *user);
void prepare_task_struct(UserData *user, TaskMsg *msg_out);
bool send_data_to_server(const char *ip, int port, const void *data_to_send, size_t data_size);
void process_server_response(UserData *user, const char *ip, int port);

// **********************************************
// ************ Utility and Persistence ************
// **********************************************

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// kbhit implementation for non-blocking key press detection
int kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;
    
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) return 0;
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) return 0;
    
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK) != 0) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return 0;
    }
    
    ch = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    
    if(ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
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

// Convert "HH:MM" string to time_t (seconds since epoch)
time_t input_time_to_seconds(const char *time_str) {
    int hour, minute;

    if (sscanf(time_str, "%d:%d", &hour, &minute) != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return 0;
    }

    time_t now = time(NULL);
    struct tm *today = localtime(&now);

    struct tm target_tm = *today;

    target_tm.tm_hour = hour;
    target_tm.tm_min = minute;
    target_tm.tm_sec = 0;

    time_t target_time = mktime(&target_tm);

    if (target_time < now) {
        target_time += 24 * 60 * 60;
    }

    return target_time;
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
}

void save_db() {
    FILE *file = fopen(DB_FILENAME, "wb");
    if (file == NULL) {
        perror("Error opening database file for writing");
        return;
    }

    size_t written = fwrite(g_dorm_db, sizeof(UserData), g_db_size, file);

    if (written != g_db_size) {
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

    int input_id;
    while (true) {
        printf("\n1. User ID: ");

        input_id = get_int_input();
        if (input_id <= 0) {
            printf("Invalid User ID\n");
            continue;
        }

        if (find_user_data(input_id) != -1) {
            printf("\nUser ID %d has existed, please choose another one\n", input_id);
            continue;
        }

        user->user_id = input_id;
        break;
    }

    while (true) {
        printf("2. Room ID: ");
        input_id = get_int_input();
        if (input_id <= 0) {
            printf("Invalid Room ID\n");
            continue;
        }

        user->room_id = input_id;
        break;
    }

    printf("\n3. Select devices:\n");

    for (int i = 0; i < MAX_AVAILABLE_DEVICES; i++) {
        printf("%d. %s\n", i + 1, g_available_devices[i].name);
    }
    printf("Devices (eg. 1, 3):  ");

    char choice_str[32];
    if (fgets(choice_str, sizeof(choice_str), stdin) == NULL) {
        choice_str[0] = '\0';
    }

    char *token = strtok(choice_str, " ,\n");
    user->device_count = 0;

    printf("\n--- Controlled devices list ---\n");

    while (token != NULL && user->device_count < 5) {
        int index = atoi(token);

        if (index >= 1 && index <= MAX_AVAILABLE_DEVICES) {
            user->control_devices[user->device_count] = g_available_devices[index - 1];
            printf("Controlled: %s\n", user->control_devices[user->device_count].name);
            user->device_count++;
        } else if (index != 0) {
            printf("Invalid : %s\n", token);
        }

        token = strtok(NULL, " ,\n");
    }

    printf("--- Sign up complete: User ID: %d, Room ID: %d ---\n", user->user_id, user->room_id);
}


bool setup_interface(UserData *user, const char *ip, int port) {
    int choice;
    int input_user_id;
    int db_index;

    TaskMsg task_to_send;

    while (true) {
        printf("==================================\n");
        printf(" Smart Dormitory System Startup\n");
        printf("==================================\n");
        printf("1. Sign up\n");
        printf("2. Sign in\n");
        printf("0. Exit\n");
        printf("Please select: ");

        choice = get_int_input();

        if (choice == 1) {
            if (g_db_size >= MAX_DORMS) {
                printf("Database is full.\n");
                continue;
            }

            initial_setup(user);

            g_dorm_db[g_db_size] = *user;
            g_db_size++;
            save_db();

            prepare_task_struct(user, &task_to_send);
            send_data_to_server(ip, port, &task_to_send, sizeof(TaskMsg));

            return true;
        } else if (choice == 2) {
            printf("\nUser ID: ");
            input_user_id = get_int_input();

            db_index = find_user_data(input_user_id);

            if (db_index != -1) {
                *user = g_dorm_db[db_index];
                printf("User data found! User ID: %d, Room ID: %d \n",
                       user->user_id, user->room_id);
                return true;
            } else {
                printf("\nCan't find User ID: %d, please check your User ID or sign up\n", input_user_id);
            }
        } else if (choice == 0) {
            printf("Exit\n");
            system("clear");
            return false;
        } else {
            printf("\nInvalid input, please try again\n");
        }
    }
}

// Feature 2: Display Device Status (CLI Version)
void display_device_status(UserData *user, const char *ip, int port) { 
    time_t now;
    struct tm *tm_info;
    char target_time_str[64];
    int db_index = find_user_data(user->user_id);

    int sock = 0;
    struct sockaddr_in serv_addr;
    device_state received_state;
    
    int state_index = user->user_id % 100; 

    while(1) { // infinite loop until Enter
        system("clear");

        now = time(NULL);

        // --- Network connection, receive and save status ---
        if (state_index >= 0 && state_index < 100 && (sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(port);
            
            if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) > 0 && 
                connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
                
                TaskMsg req = {0};
                req.user_id = user->user_id;
                send(sock, &req, sizeof(TaskMsg), 0);
                
                if (recv(sock, &received_state, sizeof(device_state), 0) == sizeof(device_state)) {
                    user->task_wash_done = (received_state.wash_m[state_index] == 1);
                    user->task_dry_done = (received_state.dry_m[state_index] == 1);
                    user->task_ac_done = (received_state.ac[state_index] == 1);
                    
                    if (db_index != -1) {
                        g_dorm_db[db_index] = *user;
                        save_db();
                    }
                }
            }
            close(sock);
        }

        printf("\n--- Device status display (User ID: %d, Room ID: %d) ---\n", user->user_id, user->room_id);
        printf("Press ENTER to return to Main Menu\n");
        printf("%-15s | %-8s | %-12s | %s\n",
           "DEVICE NAME", "STATUS", "TARGET TIME", "TASK STATUS"); 
        printf("----------------|----------|--------------|----------------\n");

        if (user->device_count == 0) {
            printf("No devices to display. Please setup first.\n");
        }

        for (int i = 0; i < user->device_count; i++) {
            Device *dev = &user->control_devices[i];

            const char *status_str = "OFF";
            char task_status_notes[32]; 
            strcpy(task_status_notes, "Ready to Schedule"); 

            bool is_completed = false;
            bool is_scheduled = (dev->target_time > 0);

            switch (dev->type) {
                case DEV_WASHER:
                    if (user->task_wash_done) is_completed = true;
                    break;
                case DEV_DRYER:
                    if (user->task_dry_done) is_completed = true;
                    break;
                case DEV_AC:
                    if (user->task_ac_done) is_completed = true;
                    break;
            }

            if (is_completed){
                strcpy(task_status_notes, "COMPLETE");
                tm_info = localtime(&dev->target_time);
                strftime(target_time_str, 64, "%H:%M", tm_info);
                status_str = "DONE";
            } else if (is_scheduled) {
                double diff_sec = difftime(dev->target_time, now);
                
                tm_info = localtime(&dev->target_time);
                strftime(target_time_str, 64, "%H:%M", tm_info);

                if (diff_sec > 0) {
                    long diff_min = (long)ceil(diff_sec / 60.0);
                    status_str = "ON";
                    snprintf(task_status_notes, sizeof(task_status_notes), "%ld minutes", diff_min);
                } else {
                    strcpy(task_status_notes, "Delayed");
                }
            } else {
                strcpy(target_time_str, "N/A");
                strcpy(task_status_notes, "Ready to Schedule");
            }

            printf("%-15s | %-8s | %-12s | %s\n",
            dev->name,
            status_str,
            is_scheduled ? target_time_str : "N/A",
            task_status_notes);
        }
        printf("----------------|----------|--------------|----------------\n");

        // Check for Enter key
        if (kbhit()) {
            int ch = getchar();
            if (ch == '\n' || ch == '\r') {
                break; // Exit loop on Enter key
            } else {
                ungetc(ch, stdin); // Push back non-Enter key
            }
        }
        
        sleep(1);
    }

    printf("\nPress Enter exit...");
    clear_input_buffer();
    getchar();
    system("clear");
}

// Feature 3: Set Estimated Time of Arrival (CLI Version)
void set_estimated_time(UserData *user) {
    char time_str[10];
    time_t future_time;
    struct tm *tm_info;
    char future_time_str[64];
    int db_index = find_user_data(user->user_id);
    char choice_str[64];

    printf("\n--- Set estimated arrival time ---\n");

    while (true){
    printf("\nEstimated Arrival time (HH:MM): ");

    if (fgets(time_str, sizeof(time_str), stdin) == NULL) {
        printf("Invalid or Failed\n");
        continue;;
    }
    time_str[strcspn(time_str, "\n")] = 0;
    future_time = input_time_to_seconds(time_str);

    if (future_time == 0) {
        printf("\nInvalid input, please check format (HH:MM)\n");
        continue;;
    }

    tm_info = localtime(&future_time);
    strftime(future_time_str, 64, "%H:%M", tm_info);
    break;
    }

    printf("\nDevices:\n");
    for (int i = 0; i < user->device_count; i++) {
        printf("  %d. %s\n", i + 1, user->control_devices[i].name);
    }
    printf("Input scheduled devices(eg. 1, 3): ");

    if (fgets(choice_str, sizeof(choice_str), stdin) == NULL) {
        printf("Cancel or Failed\n");
        return;
    }

    // Update
    char *token = strtok(choice_str, " ,\n");
    int scheduled_count = 0;

    while (token != NULL) {
        int choice_num = atoi(token);
        int index = choice_num - 1;

        if (index >= 0 && index < user->device_count) {
            user->control_devices[index].target_time = future_time;
            scheduled_count++;
            printf("-> Scheduled %s successfully\n", user->control_devices[index].name);
        }
        token = strtok(NULL, " ,\n");
    }

    if (scheduled_count > 0) {
        //printf("Scheduled %d devices successfully\n", scheduled_count);
    } else {
        system("clear");
        printf("\nNo selected device, scheduling cancel\n");
    }

    // --- Save data ---
    if (db_index != -1) {
        g_dorm_db[db_index] = *user;
        save_db();
        //printf("排程資料已儲存。\n");
    }
    printf("\nPress Enter and back to main menu...");
    getchar();
    system("clear");
}

void prepare_task_struct(UserData *user, TaskMsg *msg_out) {
    time_t latest_target_time = 0;
    bool wash_on = false;
    bool dry_on = false;
    bool ac_on = false;
    time_t now = time(NULL);

    for (int i = 0; i < user->device_count; i++) {
        Device *dev = &user->control_devices[i];

        if (dev->target_time > now) {
            if (dev->target_time > latest_target_time) {
                latest_target_time = dev->target_time;
            }

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

    memset(msg_out, 0, sizeof(TaskMsg));

    msg_out->user_id = user->user_id;
    msg_out->room_id = user->room_id;
    msg_out->original_target_time = latest_target_time;

    msg_out->task_mode.MODE_WASH = wash_on;
    msg_out->task_mode.MODE_DRY = dry_on;
    msg_out->task_mode.MODE_AC = ac_on;

    /*
    printf("TaskMsg struct prepared: ID=%d, Room=%d, Latest Target Time=%ld\n",
           msg_out->user_id, msg_out->room_id, (long)msg_out->original_target_time);
    printf("TaskMode: WASH=%d, DRY=%d, AC=%d\n",
           (int)wash_on, (int)dry_on, (int)ac_on);
    */
}

bool send_data_to_server(const char *ip, int port, const void *data_to_send, size_t data_size) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    //printf("\nAttempting to connect to server %s:%d...\n", ip, port);

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
        close(sock);
        return false;
    }

    printf(" > Connection successful! Sending Message...\n");

    long valread = send(sock, data_to_send, data_size, 0);

    if (valread < 0) {
        perror("Data sending failed");
        close(sock);
        return false;
    }

    //printf(" > Data sent successfully (%ld bytes).\n", valread);

    close(sock);
    return true;
}

// ------------------------------------------------
// Main Interface Loop (CLI Version)
// ------------------------------------------------

void run_main_interface(UserData *user, const char *ip, int port) {
    char choice_str[10];
    int choice = -1;

    // Time display variables
    time_t now;
    struct tm *tm_info;
    char time_buffer[64];

    TaskMsg task_to_send;

    system("clear");

    do {
        // --- Get and format current time ---
        now = time(NULL);
        tm_info = localtime(&now);
        strftime(time_buffer, 64, "%H:%M", tm_info);

        // --- Display main menu (text only) ---
        printf("\n========================================\n");
        printf("     Smart Dormitory System Main Menu   \n");
        printf("========================================\n");

        printf("     [Current Time: %s]\n", time_buffer);
        printf("    User ID: %d | Room ID: %d\n", user->user_id, user->room_id);
        printf("----------------------------------------\n");

        printf("1. Display Device Status\n");
        printf("2. Set Estimated Time of Arrival\n");
        printf("3. Send Task Data to Server\n");
        printf("0. Exit Main Menu\n");
        printf("----------------------------------------\n");
        printf("Please select a function (0-3): ");

        choice = get_int_input();

        system("clear");

        int index = find_user_data(user->user_id);

        // --- Execute function based on choice ---
        switch (choice) {
        case 1:
            display_device_status(user, ip, port);
            break;
        case 2:
            set_estimated_time(user);
            break;
        case 3:
            prepare_task_struct(user, &task_to_send);
            send_data_to_server(ip, port, &task_to_send, sizeof(TaskMsg));
            break;
        case 0:
            //printf("Exiting Main Menu, returning to the Startup Interface.\n");

            if (index != -1) {
                g_dorm_db[index] = *user;
                save_db();
                //printf("Data has been saved\n");
            }
            break;
        default:
            printf("Invalid input\n");
        }

    } while (choice != 0);
}

// ------------------------------------------------
// Main Program Entry Point
// ------------------------------------------------

int main(int argc, char *argv[]) {

    system("clear");

    if (argc != 3) {
        printf("Invalig address or port\n");
        printf("eg. ./client <IP Address> <Port Number>\n");
        return 1;
    }
    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    if (server_port <= 0 || server_port > 65535) {
        printf("Incorrect: Port: (1-65535)。\n");
        return 1;
    }

    load_db();

    UserData currentUser = {0};
    bool running = true;

    while (running) {
        memset(&currentUser, 0, sizeof(UserData));

        if (setup_interface(&currentUser, server_ip, server_port)) {
            run_main_interface(&currentUser, server_ip, server_port);
        } else {
            running = false;
        }
    }

    return 0;
}