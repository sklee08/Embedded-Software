#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");

#define MAX_TIMING 85
#define NUMBER 3
#define DEV_NAME "detectPi"

#define IOCTL_START_NUM 0x80
#define IOCTL_NUM1 IOCTL_START_NUM+1
#define IOCTL_NUM2 IOCTL_START_NUM+2

#define DETECT_PI_NUM 'z'
#define DETECT_PI_READ _IOWR(DETECT_PI_NUM, IOCTL_NUM1, unsigned long *)
#define DETECT_PI_DETECT _IOWR(DETECT_PI_NUM, IOCTL_NUM2, unsigned long *)

struct temp_data{
	int roomNum;
	int temp1, temp2;
	int hum1, hum2;
};

const int DHT_NUM[NUMBER] = {21, 20, 16};
const int FDSM_NUM[NUMBER] = {26, 19, 13};

int detectFire(int roomNum){
	int ret = 0;			
    ret = gpio_get_value(FDSM_NUM[roomNum]);	
    printk("status : %d\n", ret);
    return !ret;
}

static int dht11_data[5] = {0, };
static struct temp_data *data[3];

static int dht11_read(struct temp_data *param){
	int roomNum = param->roomNum;
	int find = 0;

	while(find == 0){
		int last_state = 1;
		int counter = 0;
		int i = 0, j = 0;
		int ret = 1;
		int ref = DHT_NUM[roomNum];
		dht11_data[0] = dht11_data[1] = dht11_data[2] = dht11_data[3] = dht11_data[4] = 0;

		gpio_direction_output(ref, 1);
		gpio_set_value(ref, 0);
		mdelay(18);
		gpio_set_value(ref, 1);
		udelay(40);
		gpio_direction_input(ref);

		for(i=0; i<MAX_TIMING; i++){
			counter = 0;
			while(gpio_get_value(ref) == last_state){
				counter++;
				udelay(1);

				if(counter == 255)
					break;
			}
			last_state = gpio_get_value(ref);

			if(counter == 255)
				break;

			if((i >= 4) && (i % 2 == 0)){
				dht11_data[j/8] <<= 1;
				if(counter > 16)
					dht11_data[j/8] |= 1;
				j++;
			}
		}

		if ((j >= 40) && (dht11_data[4] == ((dht11_data[0] + dht11_data[1] + dht11_data[2] + dht11_data[3]) & 0xFF))){
			data[roomNum]->roomNum = roomNum;
			data[roomNum]->hum1 = dht11_data[0];
			data[roomNum]->hum2 = dht11_data[1];
			data[roomNum]->temp1 = dht11_data[2];
			data[roomNum]->temp2 = dht11_data[3];
			ret = copy_to_user(param, data[roomNum], sizeof(struct temp_data));
			printk(" %d.%d  %d.%d C\n", data[roomNum]->temp1, data[roomNum]->temp2, data[roomNum]->hum1, data[roomNum]->hum2);
			find = 1;
		}else{
			find = 0;
		}
	}
	return 1;
}

static int readTemp(struct temp_data *param){
	if(dht11_read(param)){
		printk("Humidity: %d.%d Temperature = %d.%d C\n", dht11_data[0], dht11_data[1], dht11_data[2], dht11_data[3]);
		return 1;
	}	
	return 0;
}

int checkFire(void){
	if(dht11_data[2] > 40)
		return 1;
	return 0;
}

static int detectPi_open(struct inode *inode, struct file *file){
	printk("detectPi_open\n");
	return 0;
}

static int detectPi_release(struct inode *inode, struct file *file){
	printk("detectPi_release\n");
	return 0;
}

static long detectPi_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	int ret = 0;
	switch(cmd){		
		case DETECT_PI_READ:
			printk("Read Temp\n");			
			ret = readTemp(arg);			
			break;
		case DETECT_PI_DETECT:
			printk("Detect Fire\n");
			ret = detectFire(arg);
			break;
		default:
			return -1;
	}
	return ret;
}

struct file_operations detectPi_fops = {
	.open = detectPi_open,
	.release = detectPi_release,
	.unlocked_ioctl = detectPi_ioctl,
};

static dev_t dev_num;
static struct cdev *cd_cdev;

static int __init detectPi_init(void){
	int i;

	gpio_request(DHT_NUM[0], "DHT11");
	gpio_request(FDSM_NUM[0], "FDSM");	

	gpio_request(DHT_NUM[1], "DHT11_R1");
	gpio_request(FDSM_NUM[1], "FDSM_R1");

	gpio_request(DHT_NUM[2], "DHT11_R2");
	gpio_request(FDSM_NUM[2], "FDSM_R2");

	for(i=0; i<NUMBER; i++){
		gpio_set_value(FDSM_NUM[i], 0);
		gpio_direction_input(FDSM_NUM[i]);
	}

	alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &detectPi_fops);
	cdev_add(cd_cdev, dev_num, 1);

	for(i=0; i<NUMBER; i++){
		data[i] = (struct temp_data*)kmalloc(sizeof(struct temp_data), GFP_KERNEL);		
	}
	return 0;
}

static void __exit detectPi_exit(void){
	int i;

	for(i=0; i<NUMBER; i++){
		kfree(data[i]);
		gpio_set_value(FDSM_NUM[i], 0);
		gpio_free(FDSM_NUM[i]);
		gpio_set_value(DHT_NUM[i], 0);
		gpio_free(DHT_NUM[i]);
	}
	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num, 1);
}

module_init(detectPi_init);
module_exit(detectPi_exit);
