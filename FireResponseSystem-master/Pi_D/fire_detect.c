#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
MODULE_LICENSE("GPL");

#define LED1 4
#define SPEAKER 12

#define DEV_NAME "fire_detect_dev"

#define IOCTL_START_NUM 0x80
#define IOCTL_NUM1 IOCTL_START_NUM+1
#define IOCTL_NUM2 IOCTL_START_NUM+2

#define SIMPLE_IOCTL_NUM 'z'
#define SIMPLE_IOCTL1 _IOWR(SIMPLE_IOCTL_NUM, IOCTL_NUM1, unsigned long *)
#define SIMPLE_IOCTL2 _IOWR(SIMPLE_IOCTL_NUM, IOCTL_NUM2, unsigned long *)

static void play(int note) {
	int i = 0;
	for (i=0;i<100;i++){
		gpio_set_value(SPEAKER,1);
		udelay(note);
		gpio_set_value(SPEAKER,0);
		udelay(note);
	}
}

static void speaker_on(void){
	int notes[] = {1072,1702};
	int i=0;

	gpio_request_one(SPEAKER, GPIOF_OUT_INIT_LOW, "SPEAKER");

	for (i=0;i<10;i++) {
		play(notes[0]);
		mdelay(200);
		play(notes[1]);
		mdelay(200);
	}

}

static long simple_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	switch( cmd ){
	case SIMPLE_IOCTL1:
		printk("FIRE DETECT. LED ON\n");
		gpio_request_one(LED1,GPIOF_OUT_INIT_LOW,"LED1");
		gpio_set_value(LED1,1);
		break;
	case SIMPLE_IOCTL2:
		printk("FIRE DETECT. SPEAKER ON\n");
		speaker_on();
		break;
	default:
		return -1;
	}

	return 0;
}


static int fire_detect_open(struct inode *inode, struct file *file){
	printk("simple_ioctl open\n");
	
	gpio_request_one(LED1,GPIOF_OUT_INIT_LOW,"LED1");
	gpio_set_value(LED1,1);

	speaker_on();
	return 0;
}

static int fire_detect_release(struct inode *inode, struct file *file){
	printk("simple_ioctl release\n");
	return 0;
}

struct file_operations simple_char_fops =
{
	.unlocked_ioctl = simple_ioctl,
	.open = fire_detect_open,
	.release = fire_detect_release,
};

static dev_t dev_num;
static struct cdev *cd_cdev;

static int __init fire_detect_init(void){
	printk("Init Module\n");

	alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &simple_char_fops);
	cdev_add(cd_cdev, dev_num, 1);
	return 0;
}

static void __exit fire_detect_exit(void){
	printk("Exit Module\n");

	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num,1);
	gpio_set_value(LED1,0);
	gpio_free(LED1);

	gpio_set_value(SPEAKER,0);
	gpio_free(SPEAKER);

}

module_init(fire_detect_init);
module_exit(fire_detect_exit);





