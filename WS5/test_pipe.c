#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

#define DEVICE "/dev/mini_pipe"

int main() {
    printf("=== Starting Mini Pipe Test ===\n");
    pid_t pid = fork();

    if (pid == 0) { // Child process: Writer
        int fd = open(DEVICE, O_WRONLY);
        if (fd < 0) {
            perror("Writer open failed");
            exit(1);
        }
        printf("[Writer] Ready to write 200 bytes (Buffer size is 128)...\n");
        
        char msg[200];
        memset(msg, 'A', 199);
        msg[199] = '\0';

        int total_to_write = 200;
        int written_total = 0;

        // 修改點：使用迴圈，直到所有資料都寫入
        while(written_total < total_to_write) {
            int ret = write(fd, msg + written_total, total_to_write - written_total);
            if (ret < 0) {
                perror("Write error");
                break;
            }
            written_total += ret;
            printf("[Writer] Wrote %d bytes (Total: %d/%d). Buffer might be full now...\n", 
                   ret, written_total, total_to_write);
        }
        
        close(fd);
        printf("[Writer] Done. All data sent.\n");
        exit(0);
    } else { // Parent: Reader
        sleep(2); // 故意等待，讓 Writer 先把 Buffer 塞滿並進入 Wait 狀態
        
        int fd = open(DEVICE, O_RDONLY);
        if (fd < 0) exit(1);
        
        char buf[64];
        printf("[Reader] Starting to read (should unblock Writer)...\n");
        
        // 分次讀取
        int n = read(fd, buf, 64);
        printf("[Reader] Read %d bytes\n", n);
        sleep(1);
        n = read(fd, buf, 64);
        printf("[Reader] Read %d bytes\n", n);
        
        close(fd);
        wait(NULL);
    }
    return 0;
}