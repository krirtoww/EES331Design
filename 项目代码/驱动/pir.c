#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#define DEVICE_NAME "ccd_pir"
#define GPIO_BASE 0x41200000
#define GPIO_DATA_OFFSET 0x0
#define GPIO_TRI_OFFSET  0x4

static void __iomem *gpio_base;

// 打开设备
static int pir_open(struct inode *inode, struct file *filp)
{
    gpio_base = ioremap(GPIO_BASE, 32);
    if (!gpio_base) {
        pr_err("Failed to map GPIO base address\n");
        return -ENOMEM;
    }

    // 配置为输入模式
    iowrite32(0xFFFFFFFF, gpio_base + GPIO_TRI_OFFSET); // 所有位设为输入
    return 0;
}

// 关闭设备
static int pir_release(struct inode *inode, struct file *file)
{
    iounmap(gpio_base);
    return 0;
}

// 读取GPIO电平
static ssize_t pir_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
    int pir_val;
    pir_val = ioread32(gpio_base + GPIO_DATA_OFFSET) & 0x1;  // 只读第0位

    if (copy_to_user(buff, &pir_val, sizeof(pir_val)))
        return -EFAULT;

    return sizeof(pir_val);
}

static struct file_operations pir_fops = {
    .owner = THIS_MODULE,
    .open = pir_open,
    .release = pir_release,
    .read = pir_read,
};

static struct miscdevice pir_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &pir_fops,
};

static int __init pir_init(void)
{
    int ret = misc_register(&pir_misc_device);
    pr_info("PIR sensor driver initialized\n");
    return ret;
}

static void __exit pir_exit(void)
{
    misc_deregister(&pir_misc_device);
    pr_info("PIR sensor driver removed\n");
}

module_init(pir_init);
module_exit(pir_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccd");

