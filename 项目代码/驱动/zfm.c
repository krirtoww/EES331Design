#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "zfm_uart"
#define UART_BASE_ADDR 0x42c00000  
#define UART_MAP_SIZE 0x10000


#define UART_RX_FIFO 0x00
#define UART_TX_FIFO 0x04
#define UART_STATUS  0x08
#define UART_CTRL    0x0C


#define UART_STATUS_RX_VALID (1 << 0)
#define UART_STATUS_TX_FULL  (1 << 3)

static void __iomem *uart_base;
static dev_t devt;
static struct cdev zfm_cdev;
static struct class *zfm_class;
static struct device *zfm_device;

static int zfm_uart_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int zfm_uart_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t zfm_uart_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
    size_t i;
    u8 ch;

    for (i = 0; i < len; ++i) {
        while (!(ioread32(uart_base + UART_STATUS) & UART_STATUS_RX_VALID))
            cpu_relax();

        ch = ioread32(uart_base + UART_RX_FIFO) & 0xFF;

        if (copy_to_user(&buf[i], &ch, 1))
            return -EFAULT;
    }

    return len;
}

static ssize_t zfm_uart_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
    size_t i;
    u8 ch;

    for (i = 0; i < len; ++i) {
        if (copy_from_user(&ch, &buf[i], 1))
            return -EFAULT;

        while (ioread32(uart_base + UART_STATUS) & UART_STATUS_TX_FULL)
            cpu_relax();

        iowrite32(ch, uart_base + UART_TX_FIFO);
    }

    return len;
}

static const struct file_operations zfm_uart_fops = {
    .owner = THIS_MODULE,
    .open = zfm_uart_open,
    .release = zfm_uart_release,
    .read = zfm_uart_read,
    .write = zfm_uart_write,
};

// 发送接口
ssize_t zfm_uart_write_packet(const u8 *data, size_t len)
{
    size_t i;
    if (!uart_base)
        return -ENODEV;

    for (i = 0; i < len; ++i) {
        while (ioread32(uart_base + UART_STATUS) & UART_STATUS_TX_FULL)
            cpu_relax();
        iowrite32(data[i], uart_base + UART_TX_FIFO);
    }

    return len;
}
EXPORT_SYMBOL(zfm_uart_write_packet);

// 接收接口
#define MAX_RETRY 100000

ssize_t zfm_uart_read_packet(u8 *buf, size_t len)
{
    size_t i;
    if (!uart_base)
        return -ENODEV;

    for (i = 0; i < len; ++i) {
        int retry = 0;
        while (!(ioread32(uart_base + UART_STATUS) & UART_STATUS_RX_VALID)) {
            if (++retry > MAX_RETRY)
                return -ETIMEDOUT;
            cpu_relax();
        }
        buf[i] = ioread32(uart_base + UART_RX_FIFO) & 0xFF;
    }

    return len;
}
EXPORT_SYMBOL(zfm_uart_read_packet);

static int __init zfm_uart_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&devt, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&zfm_cdev, &zfm_uart_fops);
    ret = cdev_add(&zfm_cdev, devt, 1);
    if (ret)
        goto unregister_region;

    uart_base = ioremap(UART_BASE_ADDR, UART_MAP_SIZE);
    if (!uart_base) {
        ret = -ENOMEM;
        goto del_cdev;
    }

    zfm_class = class_create(THIS_MODULE, "zfm");
    if (IS_ERR(zfm_class)) {
        ret = PTR_ERR(zfm_class);
        goto unmap;
    }

    zfm_device = device_create(zfm_class, NULL, devt, NULL, DEVICE_NAME);
    if (IS_ERR(zfm_device)) {
        ret = PTR_ERR(zfm_device);
        class_destroy(zfm_class);
        goto unmap;
    }

    pr_info("ZFM UART driver loaded\n");
    return 0;

unmap:
    iounmap(uart_base);
del_cdev:
    cdev_del(&zfm_cdev);
unregister_region:
    unregister_chrdev_region(devt, 1);
    return ret;
}

static void __exit zfm_uart_exit(void)
{
    device_destroy(zfm_class, devt);
    class_destroy(zfm_class);
    iounmap(uart_base);
    cdev_del(&zfm_cdev);
    unregister_chrdev_region(devt, 1);
    pr_info("ZFM UART driver unloaded\n");
}

module_init(zfm_uart_init);
module_exit(zfm_uart_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NatsumeLTH");
MODULE_DESCRIPTION("ZFM UARTLite Serial Driver");

