#include "ku_pir.h"

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int ku_pir_open();
int ku_pir_close(int fd);
void ku_pir_read(int fd, struct ku_pir_data *data);
void ku_pir_flush(int fd);
int ku_pir_insertData(long unsigned int ts, char rf_flag);

int ku_pir_open(){
	int dev;
	
	dev = open("/dev/ku_pir_dev", O_RDWR);
	ioctl(dev, KU_PIR_INIT, NULL);
	
	close(dev);
	return dev;
}

int ku_pir_close(int fd){
	int dev, ret;
	
	dev = open("/dev/ku_pir_dev", O_RDWR);
	ret = ioctl(dev, KU_PIR_CLOSE, NULL);
	close(dev);
	return ret;
}

void ku_pir_read(int fd, struct ku_pir_data *data){
	int dev;

	
	dev = open("/dev/ku_pir_dev", O_RDWR);
	ioctl(dev, KU_PIR_READ, data);
	close(dev);
}

void ku_pir_flush(int fd){
	int dev;
	
	dev = open("/dev/ku_pir_dev", O_RDWR);
	ioctl(dev, KU_PIR_FLUSH, NULL);
	close(dev);
}

int ku_pir_insertData(long unsigned int ts, char rf_flag){
	int dev;
	int ret = 0;
	struct ku_pir_data* kpd;

	dev = open("/dev/ku_pir_dev", O_RDWR);

	kpd = (struct ku_pir_data*)malloc(sizeof(struct ku_pir_data));
	kpd->timestamp = ts;
	kpd->rf_flag = rf_flag;

	ret = ioctl(dev, KU_PIR_INSERTDATA, kpd);

	close(dev);
	return ret;
}
