#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEVICE_FILE "/dev/seg_dev"

int main(int argc, char *argv[]) {
    int fd;
    char *input_string;
    int len;

    if (argc != 2) {
        printf("Usage: %s <string_to_display>\n", argv[0]);
        printf("Example: %s 12345\n", argv[0]);
        return 1;
    }

    input_string = argv[1];
    len = strlen(input_string);

    fd = open(DEVICE_FILE, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open the device file");
        return 1;
    }

    printf("Writing string \"%s\" (%d bytes) to the 7-segment display...\n", input_string, len);
    int index = 0;
    

while (1)
    char char_to_write = input_string[index];

    write(fd, &char_to_write, 1);

    sleep(1); 
    
    index++;
    if (index >= len) {
        index = 0;
    }
}
        

    close(fd);
    printf("Done. The driver is now displaying the characters in sequence.\n");

    return 0;
}
