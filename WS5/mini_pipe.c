#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/version.h>  // 修正 1: 加入 version.h 解決編譯版本問題

#define DEVICE_NAME "mini_pipe"
#define BUF_SIZE 128

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");

struct mini_pipe_dev {
    dev_t dev_num;
    struct cdev cdev;
    struct class *clazz;
    struct device *device;
    
    char buffer[BUF_SIZE];
    int write_ptr;
    int read_ptr;
    int data_size; 
    
    struct mutex lock;
    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;
};

static struct mini_pipe_dev *mp_dev;

// Helper functions
static int is_empty(void) { return (mp_dev->data_size == 0); }
static int is_full(void) { return (mp_dev->data_size == BUF_SIZE); }

static int dev_open(struct inode *inodep, struct file *filep) {
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int bytes_read = 0;
    int i;

    if (mutex_lock_interruptible(&mp_dev->lock))
        return -ERESTARTSYS;

    while (is_empty()) {
        mutex_unlock(&mp_dev->lock);
        if (wait_event_interruptible(mp_dev->read_queue, !is_empty()))
            return -ERESTARTSYS;
        if (mutex_lock_interruptible(&mp_dev->lock))
            return -ERESTARTSYS;
    }

    int chunk = min((int)len, mp_dev->data_size);
    for (i = 0; i < chunk; i++) {
        if (put_user(mp_dev->buffer[mp_dev->read_ptr], buffer + i)) {
            mutex_unlock(&mp_dev->lock);
            return -EFAULT;
        }
        mp_dev->read_ptr = (mp_dev->read_ptr + 1) % BUF_SIZE;
        mp_dev->data_size--;
    }
    bytes_read = chunk;

    wake_up_interruptible(&mp_dev->write_queue);
    mutex_unlock(&mp_dev->lock);
    return bytes_read;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    int bytes_written = 0;
    int i;

    if (mutex_lock_interruptible(&mp_dev->lock))
        return -ERESTARTSYS;

    while (is_full()) {
        mutex_unlock(&mp_dev->lock);
        if (wait_event_interruptible(mp_dev->write_queue, !is_full()))
            return -ERESTARTSYS;
        if (mutex_lock_interruptible(&mp_dev->lock))
            return -ERESTARTSYS;
    }

    int space_free = BUF_SIZE - mp_dev->data_size;
    int chunk = min((int)len, space_free);

    for (i = 0; i < chunk; i++) {
        char c;
        if (get_user(c, buffer + i)) {
            mutex_unlock(&mp_dev->lock);
            return -EFAULT;
        }
        mp_dev->buffer[mp_dev->write_ptr] = c;
        mp_dev->write_ptr = (mp_dev->write_ptr + 1) % BUF_SIZE;
        mp_dev->data_size++;
    }
    bytes_written = chunk;

    wake_up_interruptible(&mp_dev->read_queue);
    mutex_unlock(&mp_dev->lock);
    return bytes_written;
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int __init mini_pipe_init(void) {
    mp_dev = kzalloc(sizeof(struct mini_pipe_dev), GFP_KERNEL);
    
    mutex_init(&mp_dev->lock);
    init_waitqueue_head(&mp_dev->read_queue);
    init_waitqueue_head(&mp_dev->write_queue);
    
    mp_dev->read_ptr = 0;
    mp_dev->write_ptr = 0;
    mp_dev->data_size = 0;

    alloc_chrdev_region(&mp_dev->dev_num, 0, 1, DEVICE_NAME);

    // 修正 2: 包含版本判斷
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    mp_dev->clazz = class_create("mini_pipe_class");
#else
    mp_dev->clazz = class_create(THIS_MODULE, "mini_pipe_class");
#endif

    mp_dev->device = device_create(mp_dev->clazz, NULL, mp_dev->dev_num, NULL, DEVICE_NAME);
    
    cdev_init(&mp_dev->cdev, &fops);
    cdev_add(&mp_dev->cdev, mp_dev->dev_num, 1);
    
    printk(KERN_INFO "Mini Pipe: Module Loaded\n");
    return 0;
}

// 修正 3: 補回你缺失的 exit 函式
static void __exit mini_pipe_exit(void) {
    cdev_del(&mp_dev->cdev);
    device_destroy(mp_dev->clazz, mp_dev->dev_num);
    class_destroy(mp_dev->clazz);
    unregister_chrdev_region(mp_dev->dev_num, 1);
    kfree(mp_dev);
    printk(KERN_INFO "Mini Pipe: Module Unloaded\n");
}

module_init(mini_pipe_init);
module_exit(mini_pipe_exit);