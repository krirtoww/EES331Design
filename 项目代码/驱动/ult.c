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


/* 驱动名称 */
#define DEVICE_NAME "ccd_ult"

/* gpio 寄存器虚拟地址 */
static void __iomem *gpio_add_minor;
static void __iomem *gpio_add_minor_led;
/* gpio 寄存器物理基地址 */
#define GPIO_BASE 0x41220000
#define GPIO_BASE_LED 0x41200000

unsigned int GPIO_TRI_Offset = 0x4;
unsigned int GPIO_DATA_Offset = 0x0;

int begin,end,OutTimeEnd,OutTimeBegin;
long ULTdis = 0;
long ULTstate = 0;

void ULTSonnic(void);
void CheckIfOutTime(void);
int XTime_GetTime(void);

static volatile int ult_res_data[2] = {0,0};

/*打开函数，当打开驱动设备节点时，执行此函数,相当于初始化设备文件*/

static int ult_open(struct inode *inode,struct file *filp)
{
	gpio_add_minor = (unsigned int)ioremap(GPIO_BASE, 32);
	gpio_add_minor_led = (unsigned int)ioremap(GPIO_BASE_LED, 32);
	return 0;
}

/* write 函数实现, 对应到 Linux 系统调用函数的 write 函数 */
static ssize_t ult_write(struct file *file_p, const char __user *buf, size_t len, loff_t *loff_t_p)
{
	return 0;
}

/* release 函数实现, 对应到 Linux 系统调用函数的 close 函数 */
static int ult_release(struct inode *inode_p, struct file *file_p)
{
	iounmap((unsigned int *)gpio_add_minor);
	return 0;
}

/* 控 制 函 数 ， 相 当 于 给 ult 发 送 控 制 命 令*/
static int ult_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/*ULTSonnic();
	ult_res_data[0] = ULTdis;
	ult_res_data[1] = ULTstate;*/
}

void ULTSonnic(void)
{

	unsigned int ult_data = (unsigned int *)(gpio_add_minor+GPIO_DATA_Offset);
	unsigned int ult_dirc_ctrl = (unsigned int *)(gpio_add_minor+GPIO_TRI_Offset);
	unsigned int led_data = (unsigned int *)(gpio_add_minor_led+GPIO_DATA_Offset);
	struct timeval ts; 


	//配置GPIO为输出模式用于启动超声波
	iowrite32(0,ult_dirc_ctrl);
	//2us低，5us高,再拉低
	iowrite32(0,ult_data);
	udelay(2);
	iowrite32(1,ult_data);
	udelay(5);
	iowrite32(0,ult_data);
	//配置GPIO为输入模式用于接收返回脉冲
	iowrite32(1,ult_dirc_ctrl);

	OutTimeBegin = XTime_GetTime();
	while(!ioread32(ult_data)){
	    	CheckIfOutTime();
		if(ULTstate == -1)
		{
			printk(KERN_DEBUG"break1");
			break;
		}
    	}

	OutTimeBegin = XTime_GetTime();
   	begin = XTime_GetTime();
   	while(ioread32(ult_data)){
   	 	CheckIfOutTime();
		if(ULTstate == -1)
		{
			printk(KERN_DEBUG"break2");
			break;
		}
   	}
   	end = XTime_GetTime();
	ULTdis = end-begin;//返回us

	return 0;
}

void CheckIfOutTime(void)
{
	int OUTTIME =400*58;
	struct timeval ts; 
	do_gettimeofday(&ts);

	OutTimeEnd = ts.tv_usec;
	if(OutTimeEnd-OutTimeBegin>OUTTIME)
		ULTstate = -1;
}
/*rewrite XTime_GetTime*/
int XTime_GetTime(void)
{
	struct timeval ts; 
	do_gettimeofday(&ts);
	int time_s = ts.tv_usec; // usecond
	return time_s;
}

/* 读函数*/
static int ult_read(struct file *filp, char __user *buff, size_t 
count, loff_t *offp)
{
	ULTdis = 0;
	ULTstate = 0;
	ULTSonnic();
	ult_res_data[0] = ULTdis;
	ult_res_data[1] = ULTstate;

	printk(KERN_DEBUG"\t%ld  %ld\nsize: %d\n",ult_res_data[0],ult_res_data[1],sizeof(ult_res_data));

	unsigned long err;
	err = copy_to_user(buff, (const void *)ult_res_data, min(sizeof(ult_res_data), count));
	return err ? -EFAULT : min(sizeof(ult_res_data), count);
}

/*struct file_operations 结构体 , 指 向 对 应 的 驱 动 操 作 函 数*/
struct file_operations ult_fops={
 .owner = THIS_MODULE,
 .open = ult_open,
 .unlocked_ioctl = ult_ioctl,
 .read = ult_read,
 .write = ult_write,
 .release = ult_release,
};
/*注册模块*/
static struct miscdevice misc = {
.minor = MISC_DYNAMIC_MINOR,
.name = DEVICE_NAME,
.fops = &ult_fops,
};


/*led 驱 动 加 载函数*/
static int __init ult_init(void)
{
int ret;
ret = misc_register(&misc);//注册设备， 混合设备驱动类，此时不需要手动分配主从设备号
printk (DEVICE_NAME"\tinitialized\n");
return ret;
}
/*led 驱 动 卸 载 函 数*/
static void __exit ult_exit(void)
{
misc_deregister(&misc);//卸载设备
}

module_init(ult_init);
module_exit(ult_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ccd");






















