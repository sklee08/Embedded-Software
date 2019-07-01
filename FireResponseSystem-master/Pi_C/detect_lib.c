#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

int dev;

void devOpen(){
	dev = open("/dev/detectPi", O_RDWR);
}

int detectFire(int roomNum){
	int ret = ioctl(dev, DETECT_PI_DETECT, roomNum);
	return ret;
}

int readData(struct temp_data *data){
	printf("lib room : %d\n", data->roomNum);
	int ret = ioctl(dev, DETECT_PI_READ, data);
	return ret;
}

void devClose(){
	close(dev);
}

// int ku_pir_insertData(long unsigned int ts, char rf_flag){
// 	struct arg_struct *arg = (struct arg_struct *)malloc(sizeof(struct arg_struct));
// 	arg->data = (struct ku_pir_data *)malloc(sizeof(struct ku_pir_data));
// 	arg->fd = dev;
// 	arg->data->timestamp = ts;
// 	arg->data->rf_flag = rf_flag;
// 	int ret = ioctl(dev, KU_PIR_INSERT, arg);
// 	return ret;
// }
