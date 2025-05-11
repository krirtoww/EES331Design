#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/time.h>
#include <asm/uaccess.h>

#define DEVICE_NAME "dip_sw"

static void __iomem *gpio_base_addr;

#define GPIO_BASE_ADDR 0x41230000
#define GPIO_DATA_OFFSET 0x0
#define GPIO_TRI_OFFSET 0x4

static unsigned char switch_state = 0;

static int sw_open(struct inode *inode, struct file *filp)
{
    // 映射GPIO寄存器
    gpio_base_addr = ioremap(GPIO_BASE_ADDR, 32);
    if (!gpio_base_addr) {
        printk(KERN_ERR "Failed to remap GPIO memory\n");
        return -ENOMEM;
    }
    
    iowrite32(0xFF, gpio_base_addr + GPIO_TRI_OFFSET);
    
    return 0;
}

static ssize_t sw_read(struct file *filp, char __user *buff, 
                      size_t count, loff_t *offp)
{
    unsigned char tmp_state;
    size_t to_copy;
    
    // 读取GPIO状态 (低8位对应8个开关)
    tmp_state = ioread32(gpio_base_addr + GPIO_DATA_OFFSET) & 0xFF;
    switch_state = tmp_state;
    
    // 确定要拷贝的数据量
    to_copy = min(sizeof(switch_state), count);
    
    // 复制到用户空间
    if (copy_to_user(buff, &switch_state, to_copy)) {
        return -EFAULT;
    }
    
    return to_copy;
}


static ssize_t sw_write(struct file *file_p, const char __user *buf, 
                       size_t len, loff_t *loff_t_p)
{
    return 0;
}


static long sw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    return 0;
}

static int sw_release(struct inode *inode_p, struct file *file_p)
{
    if (gpio_base_addr) {
        iounmap(gpio_base_addr);
        gpio_base_addr = NULL;
    }
    return 0;
}

static const struct file_operations sw_fops = {
    .owner = THIS_MODULE,
    .open = sw_open,
    .read = sw_read,
    .write = sw_write,
    .unlocked_ioctl = sw_ioctl,
    .release = sw_release,
};

static struct miscdevice sw_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &sw_fops,
};

static int __init sw_init(void)
{
    int ret;
    
    ret = misc_register(&sw_misc_device);
    if (ret) {
        printk(KERN_ERR "Failed to register misc device\n");
        return ret;
    }
    
    printk(KERN_INFO DEVICE_NAME " driver initialized\n");
    return 0;
}

static void __exit sw_exit(void)
{
    misc_deregister(&sw_misc_device);
    printk(KERN_INFO DEVICE_NAME " driver removed\n");
}

module_init(sw_init);
module_exit(sw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NatsumeLTH");
MODULE_DESCRIPTION("DIP Switch Driver ");
