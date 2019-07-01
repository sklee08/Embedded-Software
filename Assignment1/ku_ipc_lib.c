
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>

#include "ku_ipc.h"

#define DEV_NAME "ku_ipc_dev"

#define IPC_START_NUM 0x80
#define IPC_NUM1 IPC_START_NUM+1
#define IPC_NUM2 IPC_START_NUM+2
#define IPC_NUM3 IPC_START_NUM+3
#define IPC_NUM4 IPC_START_NUM+4
#define IPC_NUM5 IPC_START_NUM+5
#define IPC_NUM6 IPC_START_NUM+6
#define IPC_NUM7 IPC_START_NUM+7
#define IPC_NUM8 IPC_START_NUM+8
#define IPC_NUM9 IPC_START_NUM+9
#define SIMPLE_IPC_NUM 'z'
#define KU_IPC_SEND 		_IOWR(SIMPLE_IPC_NUM, IPC_NUM1, unsigned long *)
#define KU_IPC_RECEIVE 		_IOWR(SIMPLE_IPC_NUM, IPC_NUM2, unsigned long *)
#define KU_IPC_MSGGET	 	_IOWR(SIMPLE_IPC_NUM, IPC_NUM3, unsigned long *)
#define KU_IPC_MSGCLOSE 	_IOWR(SIMPLE_IPC_NUM, IPC_NUM4, unsigned long *)
#define KU_IPC_EXIST_KEY	_IOWR(SIMPLE_IPC_NUM, IPC_NUM5, unsigned long *)
#define KU_IPC_CREATE_QUEUE 	_IOWR(SIMPLE_IPC_NUM, IPC_NUM6, unsigned long *)
#define KU_IPC_FULL_QUEUE 	_IOWR(SIMPLE_IPC_NUM, IPC_NUM7, unsigned long *)
#define KU_IPC_NO_QUEUE 	_IOWR(SIMPLE_IPC_NUM, IPC_NUM8, unsigned long *)
#define KU_IPC_EMPTY_QUEUE	_IOWR(SIMPLE_IPC_NUM, IPC_NUM9, unsigned long *)


struct msgbuf{
	long type;	
	char text[100];
};

struct ipcbuf{
	int msqid;
	void *msgp;
	int msgsz;
	long msgtyp;
	int msgflg;
};


int ku_msgget(int key, int msgflg);
int ku_msgclose(int msqid);
int ku_msgsnd(int msqid, void *msgp, int msgsz, int msgflg);
int ku_msgrcv(int msqid, void *msgp, int msgsz, long msgtyp, int msgflg);


int ku_msgget(int key, int msgflg){
	int dev;
	int ret;
	int isExist;
	dev = open("/dev/ku_ipc_dev", O_RDWR);
	isExist = ioctl(dev, KU_IPC_EXIST_KEY, &key);
	if(msgflg == IPC_EXCL){
		if(isExist == -1) ret = ioctl(dev, KU_IPC_CREATE_QUEUE, &key);
		else {
			if(isExist) ret = -1;
			else ret = ioctl(dev, KU_IPC_CREATE_QUEUE, &key);		
		}
	} else {
		if(isExist == -1) ret = ioctl(dev, KU_IPC_CREATE_QUEUE, &key);
		else {
			if(isExist) ret = isExist;
			else ret = ioctl(dev, KU_IPC_CREATE_QUEUE, &key);		
		}	
	}
	close(dev);
	return ret;
}


int ku_msgclose(int msqid){
	int dev;
	int ret;
	dev = open("/dev/ku_ipc_dev", O_RDWR);
	ret = ioctl(dev, KU_IPC_MSGCLOSE, &msqid);
	close(dev);
	return ret;
}

int ku_msgsnd(int msqid, void *msgp, int msgsz, int msgflg){
	int dev;
	int ret = 0;
	int remain_byte;
	int full_queue;
	struct msgbuf *msg;
	struct ipcbuf ipc;

	dev = open("/dev/ku_ipc_dev", O_RDWR);
	msg = (struct msgbuf *)msgp;
	ipc.msqid = msqid;
	ipc.msgp = msgp;
	ipc.msgsz = msgsz;
	ipc.msgflg = msgflg;

	full_queue = ioctl(dev, KU_IPC_FULL_QUEUE, &msqid);
	if(full_queue){
		if(msgflg & IPC_NOWAIT){
		ret = -1;
		return ret;
		} else {
		while(ioctl(dev, KU_IPC_FULL_QUEUE, &msqid) || ioctl(dev, KU_IPC_NO_QUEUE));
		}	
	}

	remain_byte = ioctl(dev, KU_IPC_SEND, &ipc);
	close(dev);
	return ret;
		
}

int ku_msgrcv(int msqid, void *msgp, int msgsz, long msgtyp, int msgflg){
	int dev;
	int ret= 0;
	int remain_byte;
	int empty_queue;
	int rcv_msg_size;
	struct msgbuf *msg;
	struct ipcbuf ipc;
	char ch[msgsz];

	dev = open("/dev/ku_ipc_dev", O_RDWR);
	ipc.msqid = msqid;
	ipc.msgp = msgp;
	ipc.msgsz = msgsz;
	ipc.msgtyp = msgtyp;
	ipc.msgflg = msgflg;

	empty_queue = ioctl(dev, KU_IPC_EMPTY_QUEUE, &msqid);
	
	if(empty_queue){
		if(msgflg & IPC_NOWAIT){
			ret=-1;
			return ret;
		} else {
			while(ioctl(dev, KU_IPC_EMPTY_QUEUE, &msqid) || ioctl(dev, KU_IPC_NO_QUEUE));	// wait for msg to come
		}

	}

	remain_byte = ioctl(dev, KU_IPC_RECEIVE, &ipc);
	
	msg = (struct msgbuf*)ipc.msgp;
	ret = strlen(msg->text);

	rcv_msg_size = sizeof(*msg);

	if(msgsz < rcv_msg_size){
		if(!(msgflg & MSG_NOERROR)){
			ret = -1;
		}
	}

	close(dev);
	return ret;
}

