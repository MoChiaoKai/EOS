#include <linux/init.h> 
#include <linux/module.h> 
#include <linux/kernel.h> 
#include <linux/fs.h> 
#include <linux/uaccess.h> 
#include <linux/slab.h> 
#include <linux/string.h> 

#define MAJOR_NUM 126
#define DEVICE_NAME "/dev/seg_dev" 
#define DATA_SIZE 17 

static unsigned short current_segment_data = 0;

static const unsigned short seg_for_c[27] = {
    0b1111001100010001, // A
    0b0000011100000101, // b
    0b1100111100000000, // C
    0b0000011001000101, // d
    0b1000011100000001, // E
    0b1000001100000001, // F
    0b1001111100010000, // G
    0b0011001100010001, // H
    0b1100110001000100, // I
    0b1100010001000100, // J
    0b0000000001101100, // K
    0b0000111100000000, // L
    0b0011001110100000, // M
    0b0011001110001000, // N
    0b1111111100000000, // O
    0b1000001101000001, // P
    0b0111000001010000, //q
    0b1110001100011001, //R
    0b1101110100010001, //S
    0b1100000001000100, //T
    0b0011111100000000, //U
    0b0000001100100010, //V
    0b0011001100001010, //W
    0b0000000010101010, //X
    0b0000000010100100, //Y
    0b1100110000100010, //Z
    0b0000000000000000
 };


static int my_open(struct inode *inode, struct file *fp) { 
    pr_info("Lab4 Driver: Device %s opened.\n", DEVICE_NAME); 
    return 0;
} 


static ssize_t my_read(struct file *fp, char __user *buf, size_t count, loff_t *fpos) { 
    char output_buffer[DATA_SIZE]; // 17 bytes: 16 chars + null terminator
    int i;
    
    for (i = 0; i < 16; i++) {
        if (current_segment_data & (1 << (15 - i))) {
            output_buffer[i] = '1';
        } else {
            output_buffer[i] = '0';
        }
    }
    
    // 設置字串結尾（雖然 reader 不一定需要，但這是 C 語言的最佳實踐）
    output_buffer[16] = '\0'; 

    // 將 17 Bytes 數據複製到使用者空間
    if (copy_to_user(buf, output_buffer, DATA_SIZE)) {
        pr_err("Lab4 Driver: Failed to copy data to user space.\n");
        return -EFAULT;
    }
    
    pr_info("Lab4 Driver: Read data: %s to user space.\n", output_buffer); 

    // 回傳實際傳輸的位元組數 (17 bytes)
    return DATA_SIZE; 
} 

static ssize_t my_write(struct file *fp, const char __user *buf, size_t count, loff_t *fpos) { 
    char received_char;
    int index;

    if (copy_from_user(&received_char, buf, 1)) {
        pr_err("Lab4 Driver: Failed to copy data from user space.\n");
        return -EFAULT;
    }

    if (received_char >= 'A' && received_char <= 'Z') {
        index = received_char - 'A';
    } else if (received_char >= 'a' && received_char <= 'z') {
        index = received_char - 'a';
    } else {
        index = 26;
    }

    // 4. 更新核心數據狀態
    current_segment_data = seg_for_c[index];
    
    pr_info("Lab4 Driver: Wrote char '%c', new data: 0x%04x.\n", received_char, current_segment_data); 
    
    // 5. 回傳實際寫入的位元組數
    return 1; 
} 

/**
 * @brief 裝置釋放函式。
 */
static int my_release(struct inode *inode, struct file *fp) {
    printk(KERN_INFO "Lab4 Driver: Device %s closed.\n", DEVICE_NAME);
    return 0;
}

struct file_operations my_fops = { 
    .owner   = THIS_MODULE,
    .read    = my_read, 
    .write   = my_write, 
    .open    = my_open,
    .release = my_release,
}; 



static int __init my_init(void) { 
    pr_info("Lab4 Driver: Calling init.\n"); 
    
    // 註冊字元裝置
    if (register_chrdev(MAJOR_NUM, DEVICE_NAME, &my_fops) < 0) { 
        pr_err("Lab4 Driver: Cannot get major %d.\n", MAJOR_NUM); 
        return -EBUSY; 
    } 
    
    pr_info("Lab4 Driver: Device %s started, major is %d.\n", 
           DEVICE_NAME, MAJOR_NUM); 
    
    return 0;
} 

static void __exit my_exit(void) { 
    // 解除註冊字元裝置
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME); 
    pr_info("Lab4 Driver: Device %s unregistered. Calling exit.\n", DEVICE_NAME); 
} 


module_init(my_init); 
module_exit(my_exit);
