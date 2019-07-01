#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <asm/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include "ku_ipc.h"

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
#define DEV_NAME "ku_ipc_dev"

MODULE_LICENSE("GPL");

int is_key(int *key);
int make_queue(int *key);
int remove_queue(int *msqid);
struct q* get_queue(int key);
int is_full(int *msqid);
int is_empty(int *msqid);
int get_queue_size(int msqid);
int get_num(int msqid);
int no_queue(int *msqid);



spinlock_t lock;

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


struct q{
	struct list_head list;
	struct msgbuf kern_buf;
};

struct queues{
	struct list_head list;
	struct q kern_q;
	int key;
};

struct queues kern_queue;



int is_key(int *key){
	struct queues *tmp = 0;
	int ret = 0;
	
	spin_lock(&lock);
	list_for_each_entry(tmp, &kern_queue.list, list){
		if(tmp->key == *key){
			ret = tmp->key;		
		}
	}

	spin_unlock(&lock);
	return ret;
}

int make_queue(int *key){
	struct queues *tmp = 0;
	tmp = (struct queues*)kmalloc(sizeof(struct queues), GFP_KERNEL);
	
	tmp->key = *key;

	INIT_LIST_HEAD(&tmp->kern_q.list);

	spin_lock(&lock);
	list_add(&tmp->list, &kern_queue.list);
	spin_unlock(&lock);

	return tmp->key;
}

int remove_queue(int *msqid){

	struct queues *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *next = 0;
	int ret = -1;
	
	spin_lock(&lock);
	list_for_each_safe(pos, next, &kern_queue.list){
		tmp = list_entry(pos, struct queues, list);
		if(tmp->key == *msqid){
			list_del(pos);
			kfree(tmp);
			ret = 0;		
		}
	}

	spin_unlock(&lock);
	return ret;
}

struct q* get_queue(int key){
	int i = 0;
	struct queues *tmp = 0;
	struct q *ret = 0;

	spin_lock(&lock);
	list_for_each_entry(tmp, &kern_queue.list, list){
		if(tmp->key == key){
			ret = &tmp->kern_q;
		}
		i++;
	}	
	spin_unlock(&lock);
	return ret;
}

int is_full(int *msqid){
	int ret = 0;
	struct q *tmp = get_queue(*msqid);

	if(get_queue_size(*msqid) >= KUIPC_MAXVOL || get_num(*msqid) == KUIPC_MAXMSG) ret = 1;
	return ret;
}

int is_empty(int *msqid){
	int ret = 0;
	struct q *tmp = get_queue(*msqid);

	if(get_num(*msqid) == 0) ret = 1;
	return ret;
}

int get_queue_size(int msqid){
	unsigned int ret;
	struct q *tmp = 0;
	struct q *tar = get_queue(msqid);
	ret = 0;
	
	spin_lock(&lock);
	list_for_each_entry(tmp, &tar->list, list){
		ret += sizeof(*tmp);
	}
	spin_unlock(&lock);
	return ret;
}

int get_num(int msqid){
	unsigned int ret;
	struct q *tmp = 0;
	struct q *tar = get_queue(msqid);
	unsigned int i;
	ret = 0;
	i = 0;
	spin_lock(&lock);
	list_for_each_entry(tmp, &tar->list, list){
		i++;	
	}
	ret= i;
	spin_unlock(&lock);

	return ret;
}

int no_queue(int *msqid){
	unsigned int ret;
	struct q *tar = get_queue(*msqid);
	ret = 0;
	if(tar == NULL) ret = 1;
	return ret;
}


static int read_ipc(struct ipcbuf *ipc){
	int ret;
	unsigned int i = 0;
	struct q *tmp;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	struct q *read = 0;
	long type = 0L;

	if(no_queue(&ipc->msqid)){
		// this msqid's queue empty
		return -1;
	}

	read = get_queue(ipc->msqid);

	spin_lock(&lock);
	list_for_each_safe(pos, q, &read->list){
		tmp = list_entry(pos, struct q, list);
		type = ipc->msgtyp;
		if(type == tmp->kern_buf.type){
				if(i==0){
					ret = copy_to_user(ipc->msgp, &tmp->kern_buf, ipc->msgsz);
					list_del(pos);
					kfree(tmp);
				}
				i++;
			}	

	}

	spin_unlock(&lock);


	return ret;
	
}

static int write_ipc(struct ipcbuf *ipc){
	int ret;
	struct q *tmp = 0;
	struct q *write = 0;
	struct msgbuf *user_msgbuf;
	
	user_msgbuf = (struct msgbuf *)ipc->msgp;

	if(no_queue(&ipc->msqid)) return -1;
	
	write= get_queue(ipc->msqid);

	tmp = (struct q*)kmalloc(sizeof(struct q), GFP_KERNEL);

	ret = copy_from_user(&tmp->kern_buf, user_msgbuf, ipc->msgsz);
	
	spin_lock(&lock);
	list_add_tail(&tmp->list, &write->list);
	spin_unlock(&lock);

	return ret;
}

static long ku_ipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	struct msgbuf *user_buf;
	struct ipcbuf *ipc;
	long ret = 0L;

	user_buf = (struct msgbuf *)arg;

	switch(cmd){
		case KU_IPC_RECEIVE :
			ipc = (struct ipcbuf *)arg;
			ret = read_ipc(ipc);
			break;	
		case KU_IPC_SEND :
			ipc = (struct ipcbuf *)arg;
			ret = write_ipc(ipc);
			break;
		case KU_IPC_EXIST_KEY :
			ret = is_key((int*)arg);
			break;
		case KU_IPC_CREATE_QUEUE :
			ret = make_queue((int*) arg);
			break;
		case KU_IPC_MSGCLOSE :
			ret = remove_queue((int*) arg);
			break;
		case KU_IPC_FULL_QUEUE :
			ret = is_full((int *) arg);
 			break;
		case KU_IPC_NO_QUEUE :
			ret = no_queue((int *) arg);
			break;
		case KU_IPC_EMPTY_QUEUE :
			ret = is_empty((int *) arg);
			break;
	}

	return ret;
}

struct file_operations ku_ipc_fops =
{
	.unlocked_ioctl = ku_ipc_ioctl,
};

static dev_t dev_num;
static struct cdev *cd_cdev;

static int __init ku_ipc_init(void) {
	int ret;
	printk("Init Module\n");
	INIT_LIST_HEAD(&kern_queue.list);
	alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &ku_ipc_fops);
	ret = cdev_add(cd_cdev, dev_num, 1);

	return ret;
}
static void __exit ku_ipc_exit(void){
	struct queues *tmp;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	printk("Exit Module\n");
	list_for_each_safe(pos, q, &kern_queue.list){
		tmp = list_entry(pos, struct queues, list);
		list_del(pos);
		kfree(tmp);
	}
	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num, 1);
}

module_init(ku_ipc_init);
module_exit(ku_ipc_exit);
