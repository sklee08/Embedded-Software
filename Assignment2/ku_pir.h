#define KU_PIR_START_NUM 0x80
#define KU_PIR_NUM1 KU_PIR_START_NUM+1
#define KU_PIR_NUM2 KU_PIR_START_NUM+2
#define KU_PIR_NUM3 KU_PIR_START_NUM+3
#define KU_PIR_NUM4 KU_PIR_START_NUM+4
#define KU_PIR_NUM5 KU_PIR_START_NUM+5

#define KU_PIR_NUM 'z'
#define	KU_PIR_INSERTDATA	_IOWR(KU_PIR_NUM, KU_PIR_NUM1, unsigned long *)
#define	KU_PIR_READ		_IOWR(KU_PIR_NUM, KU_PIR_NUM2, unsigned long *)
#define	KU_PIR_CLOSE		_IOWR(KU_PIR_NUM, KU_PIR_NUM3, unsigned long *)
#define	KU_PIR_INIT		_IOWR(KU_PIR_NUM, KU_PIR_NUM4, unsigned long *)
#define	KU_PIR_FLUSH		_IOWR(KU_PIR_NUM, KU_PIR_NUM5, unsigned long *)

#define KUPIR_MAX_MSG 15
#define KUPIR_SENSOR 17

#define DEV_NAME "ku_pir_dev"

struct ku_pir_data{
	long unsigned int timestamp;
	char rf_flag;
};



