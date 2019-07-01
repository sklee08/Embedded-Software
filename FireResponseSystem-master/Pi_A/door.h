#define MAX_DATA 10

#define IOCTL_START_NUM 0x80
#define IOCTL_NUM1 IOCTL_START_NUM+1	// readData()
#define IOCTL_NUM2 IOCTL_START_NUM+2	// openDoor()
#define IOCTL_NUM3 IOCTL_START_NUM+3	// closeDoor()
#define IOCTL_NUM4 IOCTL_START_NUM+4	// isFired()

#define SIMPLE_IOCTL_NUM 'z'
#define SIMPLE_IOCTL1 _IOWR(SIMPLE_IOCTL_NUM, IOCTL_NUM1, unsigned long *)
#define SIMPLE_IOCTL2 _IOWR(SIMPLE_IOCTL_NUM, IOCTL_NUM2, unsigned long *)
#define SIMPLE_IOCTL3 _IOWR(SIMPLE_IOCTL_NUM, IOCTL_NUM3, unsigned long *)
#define SIMPLE_IOCTL4 _IOWR(SIMPLE_IOCTL_NUM, IOCTL_NUM4, unsigned long *)

struct sensor_data {
    long unsigned int timestamp;    // 센서에 저장되는 timestamp
    int rf_flag;   // pir센서의 RISING, FALLING 여부(0: R, 1: F)
                    // 초음파 센서의 거리감지(0: 물체감지, 1)
};

