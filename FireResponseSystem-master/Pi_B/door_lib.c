#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "door.h"

int dev;

void start(){

dev = open("/dev/door_dev", O_RDWR);

}

void end(){
close(dev);
}

int readData(){
    int ret;

	

	struct sensor_data value;

	ret = ioctl(dev, SIMPLE_IOCTL1, &value);

	return ret;
}

void openDoor(){


	struct sensor_data value;

    ioctl(dev, SIMPLE_IOCTL2, &value);


}

void closeDoor(){
 

	struct sensor_data value;

    ioctl(dev, SIMPLE_IOCTL3, &value);

 
}

void detectFire(){
	ioctl(dev, SIMPLE_IOCTL4, NULL);

}
