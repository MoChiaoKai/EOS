#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h> //copy_to/from_user()
#include <linux/gpio.h> //GPIO

// GPIOs for each segment of the 7-segment display (A to G)
#define SEG_A_GPIO 21
#define SEG_B_GPIO 20
#define SEG_C_GPIO 16
#define SEG_D_GPIO 12
#define SEG_E_GPIO 25
#define SEG_F_GPIO 24
#define SEG_G_GPIO 23

// Array to hold the GPIO numbers for each segment
static int segment_gpios[] = {
    SEG_A_GPIO, SEG_B_GPIO, SEG_C_GPIO, SEG_D_GPIO,
    SEG_E_GPIO, SEG_F_GPIO, SEG_G_GPIO
};

#define NUM_DEVICES ARRAY_SIZE(segment_gpios)

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);

/*************** Driver functions **********************/
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);
/******************************************************/
//File operation structure
static struct file_operations fops =
{
.owner = THIS_MODULE,
.read = etx_read,
.write = etx_write,
.open = etx_open,
.release = etx_release,
};

/**
 * @brief Maps a character to the correct 7-segment configuration
 * @param c The character to display.
 */
void segment_map(char c)
{
    // Common anode display logic (1 = ON, 0 = OFF)
    int digits[11][7] = {
        // A, B, C, D, E, F, G
        {1, 1, 1, 1, 1, 1, 0}, // 0
        {0, 1, 1, 0, 0, 0, 0}, // 1
        {1, 1, 0, 1, 1, 0, 1}, // 2
        {1, 1, 1, 1, 0, 0, 1}, // 3
        {0, 1, 1, 0, 0, 1, 1}, // 4
        {1, 0, 1, 1, 0, 1, 1}, // 5
        {1, 0, 1, 1, 1, 1, 1}, // 6
        {1, 1, 1, 0, 0, 0, 0}, // 7
        {1, 1, 1, 1, 1, 1, 1}, // 8
        {1, 1, 1, 1, 0, 1, 1}, // 9
        {1, 0, 0, 1, 1, 1, 1}, // E
    };
    int i, val;
    int index = -1;

    if (c >= '0' &&  c <= '9') {
        index = c - '0';
    }
    else if (c =='e' ||c =='E'){
	index = 10;
    }
    if (index != -1) {
        for (i = 0; i < NUM_DEVICES; i++) {
            val = digits[index][i];
            gpio_set_value(segment_gpios[i], val);
        }
    } else {
        pr_err("Invalid character: %c. Displaying blank.\n", c);
        // Turn off all segments for invalid input
        for (i = 0; i < NUM_DEVICES; i++) {
            gpio_set_value(segment_gpios[i], 0);
        }
    }
}

/**
 * @brief This function will be called when we open the Device file.
 */
static int etx_open(struct inode *inode, struct file *file)
{
    pr_info("Device File Opened...!!!\n");
    return 0;
}

/**
 * @brief This function will be called when we close the Device file.
 */
static int etx_release(struct inode *inode, struct file *file)
{
    pr_info("Device File Closed...!!!\n");
    return 0;
}

/**
 * @brief This function will be called when we read the Device file.
 * We do not support read operation for this device.
 */
static ssize_t etx_read(struct file *filp,
char __user *buf, size_t len, loff_t *off)
{
    pr_info("Read operation not supported for this device.\n");
    return 0;
}

/**
 * @brief This function will be called when we write the Device file.
 * It receives a full string from user space and displays it character by character.
 */
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    // A dynamic array to hold the incoming string
    uint8_t *rec_buf;
    int i;
    
    // Allocate memory for the buffer
    rec_buf = kmalloc(len, GFP_KERNEL);
    if (!rec_buf) {
        pr_err("Failed to allocate memory for buffer.\n");
        return -ENOMEM;
    }

    // Copy the entire string from user space
    if (copy_from_user(rec_buf, buf, len) > 0) {
        pr_err("ERROR: Not all bytes have been copied from user\n");
        kfree(rec_buf);
        return -EFAULT;
    }

    pr_info("Write Function: Received string \"%.*s\"\n", (int)len, rec_buf);

    // Iterate through the string, display each character with a delay
    for (i = 0; i < len; i++) {
        segment_map(rec_buf[i]);
        msleep(1000); // Delay for 500 ms to observe the display change
    }
    
    // Free the allocated memory
    kfree(rec_buf);

    return len; // Return the number of bytes written
}

/**
 * @brief Module Init function
 * Allocates a major number, creates 7 devices, and requests 7 GPIOs.
 */
static int __init etx_driver_init(void)
{
    int i;
    int ret;

    /* Allocating Major number */
    if ((ret = alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
        pr_err("Cannot allocate major number\n");
        return ret;
    }
    pr_info("Major = %d\n, Minor = %d\n", MAJOR(dev), MINOR(dev));

    /* Creating struct class */
    if ((dev_class = class_create(THIS_MODULE, "etx_class")) == NULL) {
        pr_err("Cannot create the struct class\n");
        goto r_unreg;
    }
    cdev_init(&etx_cdev, &fops);
    cdev_add(&etx_cdev, dev, 1);
    device_create(dev_class, NULL, dev, NULL, "etx_device");
    
    // Loop to initialize and create device files for each GPIO
    for (i = 0; i < NUM_DEVICES; i++) {
        if (gpio_is_valid(segment_gpios[i]) == false) {
            pr_err("GPIO %d is not valid\n", segment_gpios[i]);
            goto r_dev_destroy;
        }

        if ((ret = gpio_request(segment_gpios[i], "7_SEGMENT_GPIO")) < 0) {
            pr_err("ERROR: GPIO %d request failed\n", segment_gpios[i]);
            goto r_dev_destroy;
        }

        gpio_direction_output(segment_gpios[i], 0); // Set to OFF for common anode

        /*cdev_init(&etx_cdev[i], &fops);
        etx_cdev[i].owner = THIS_MODULE;

        if ((ret = cdev_add(&etx_cdev[i], MKDEV(MAJOR(dev), i), 1)) < 0) {
            pr_err("Cannot add the device to the system (minor %d)\n", i);
            goto r_dev_destroy;
        }

        if (device_create(dev_class, NULL, MKDEV(MAJOR(dev), i), NULL, "etx_device%d", i) == NULL) {
            pr_err("Cannot create the Device file (minor %d)\n", i);
            goto r_dev_destroy;
        }*/
    }
    
    pr_info("Device Driver Insert...Done!!!\n");
    return 0;

r_dev_destroy:
	int j;
	cdev_del(&etx_cdev);
    for (j = 0; j < i; j++) {
        device_destroy(dev_class, dev);        
        gpio_free(segment_gpios[j]);
    }
    class_destroy(dev_class);
r_unreg:
    unregister_chrdev_region(dev, NUM_DEVICES);
    return -1;
}

/**
 * @brief Module exit function
 * Cleans up all resources, freeing GPIOs and destroying devices.
 */
static void __exit etx_driver_exit(void)
{
    int i;
    cdev_del(&etx_cdev);
    device_destroy(dev_class, dev);
    for (i = 0; i < NUM_DEVICES; i++) {
        gpio_set_value(segment_gpios[i], 1); // Turn off all segments
        gpio_free(segment_gpios[i]);       
    }
    class_destroy(dev_class);
    unregister_chrdev_region(dev, NUM_DEVICES);
    pr_info("Device Driver Remove...Done!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver for 7-segment display");
MODULE_VERSION("1.34");
