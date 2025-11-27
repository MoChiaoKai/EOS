#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEVICE_FILE "/dev/etx_device" // 選擇一個驅動程式建立的設備檔案

int main(int argc, char *argv[]) {
    int fd;
    char *input_string;
    int len;

    // 檢查命令列參數是否正確
    if (argc != 2) {
        printf("Usage: %s <string_to_display>\n", argv[0]);
        printf("Example: %s 12345\n", argv[0]);
        return 1;
    }

    input_string = argv[1];
    len = strlen(input_string);

    // 開啟設備檔案，只使用寫入模式
    fd = open(DEVICE_FILE, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open the device file");
        return 1;
    }

    printf("Writing string \"%s\" (%d bytes) to the 7-segment display...\n", input_string, len);

    // 將整個字串一次性寫入設備檔案
    if (write(fd, input_string, len) < 0) {
        perror("Failed to write to the device file");
        close(fd);
        return 1;
    }

    // 關閉設備檔案
    close(fd);
    printf("Done. The driver is now displaying the characters in sequence.\n");

    return 0;
}
