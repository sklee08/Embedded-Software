#include "ku_pir.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/rculist.h>
#include <linux/sched.h>
MODULE_LICENSE("GPL");

struct queue{
	struct list_head list;
	struct ku_pir_data data;
};

struct queue_list{
	struct list_head list;
	struct queue q;
	pid_t pid;
};

spinlock_t my_lock;
wait_queue_head_t my_wq;
static int irq_num;
static dev_t dev_num;
static struct cdev *cd_cdev;
struct queue_list my_queues;

pid_t init_queue(void);
struct queue *get_queue(pid_t pid);
int is_not_exist(pid_t pid);
int get_num(pid_t pid);
int is_full(pid_t pid);
int remove_queue(void);
int remove_queue_list(void);
void remove_old_queue(pid_t pid);
void insert_from_isr(long unsigned int timestamp, char rf_flag);
void ku_pir_read(struct ku_pir_data *kpd);
int insert_from_user(struct ku_pir_data * kpd);


pid_t init_queue(){
	
	// to init the queue_list for the pid by rcu ways //

	struct queue_list *tmp = 0;
	tmp = (struct queue_list*)kmalloc(sizeof(struct queue_list), GFP_KERNEL);
	tmp->pid = current->pid;
//	printk("pid is %d\n", tmp->pid);	

	INIT_LIST_HEAD(&tmp->q.list);

	spin_lock(&my_lock);
	list_add_rcu(&tmp->list, &my_queues.list);
	spin_unlock(&my_lock);
	synchronize_rcu();

	return tmp->pid;
	
}


struct queue* get_queue(pid_t pid){

	// find the pid's queue frome queue_list
	
	struct queue_list *tmp = 0;
	struct queue *get = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(tmp, &my_queues.list, list){
		if(tmp->pid == pid){
			get = &tmp->q;		
		}
	}
	rcu_read_unlock();
	
	return get;
}



int is_not_exist(pid_t pid){
	
	// if pid's queue does not exist then return 1, if exist then return 0
	
	struct queue *tar = 0;
	tar = get_queue(pid);
	if(tar == NULL) return 1;
	else return 0;
}

int get_num(pid_t pid){

	// to find the number of data from pid's queue's data structure 	

	int ret = 0;
	struct queue *tmp = 0;
	struct queue *tar = 0;
	if(is_not_exist(pid)) return -1;			// if there are no pid queue return -1
	tar = get_queue(pid);
	
	spin_lock(&my_lock);
	list_for_each_entry(tmp, &tar->list, list){
		ret++;
	}

	spin_unlock(&my_lock);
	return ret;
}

int is_full(pid_t pid){

	// if pid's queue is more than KUPIR_MAX_MSG return 1	

	if(get_num(pid) >= KUPIR_MAX_MSG) return 1;
	else return 0;
}



int remove_queue(){

	// remove all the queue of pid's queue_list 	

	struct queue *tar = 0;
	struct queue *tmp = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	pid_t pid;
	pid = current->pid;
	int ret = -1;
	
	if(is_not_exist(pid)) return ret;

	tar = get_queue(pid);
	
	spin_lock(&my_lock);
	list_for_each_safe(pos, q, &tar->list){
		tmp = list_entry(pos, struct queue, list);
		list_del(pos);
		kfree(tmp);
		ret = 0;	
	}
	spin_unlock(&my_lock);

	return ret;
}



int remove_queue_list(){
	
	// remove the pid's queue_list 
	
	int ret = -1;	
	struct queue_list *tmp = 0;
	struct list_head *pos;
	pid_t pid;
	pid = current->pid;

	if(is_not_exist(pid)){
		return ret;	
	}

	remove_queue();			// remove all queue in pid's queue_list

	spin_lock(&my_lock);
	list_for_each_entry_rcu(tmp, &my_queues.list, list){
		if(tmp->pid == pid){
			list_del_rcu(&tmp->list);
			kfree(tmp);
			break;
		}	
	}
	synchronize_rcu();
	if(is_not_exist(pid)) ret = 0;
	spin_unlock(&my_lock);
	
	return ret;
}




void remove_old_queue(pid_t pid){

	// remove the oldest queue in the pid's queue

	struct queue *tmp = 0;
	struct queue *tar = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;	

	tar = get_queue(pid);

	spin_lock(&my_lock);
	list_for_each_safe(pos, q, &tar->list){
		tmp = list_entry(pos, struct queue, list);
		list_del(pos);
		kfree(tmp);
		break;
	}
	spin_unlock(&my_lock);


}

void insert_from_isr(long unsigned int timestamp, char rf_flag){

	// insert data(ts, rf_flag) to all queue_list's queues
	
	struct queue_list *tmp = 0;
	struct queue *q = 0;
	
	rcu_read_lock();
	list_for_each_entry_rcu(tmp, &my_queues.list, list){
		q = (struct queue*)kmalloc(sizeof(struct queue), GFP_KERNEL);
		q->data.timestamp = timestamp;
		q->data.rf_flag = rf_flag;
		if(is_full(tmp->pid)) remove_old_queue(tmp->pid);		// if data is full remove
		
		spin_lock(&my_lock);
		list_add_tail_rcu(&q->list, &tmp->q.list);
		spin_unlock(&my_lock);
		
		
	}
	rcu_read_unlock();
	wake_up_interruptible(&my_wq);
	
}

void ku_pir_read(struct ku_pir_data* kpd){

	// read data from queue_list // copy_to_user
	
	struct queue *tmp = 0;
	struct queue *ret_queue = 0;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	pid_t pid;
	pid = current->pid;
	
	// wait the processes for condition for the data to come 

	wait_event_interruptible(my_wq, (get_num(pid)>0) || (get_num(pid) == -1));

	if(is_not_exist(pid)) return;
	ret_queue = get_queue(pid);
//	printk("pid is %d\n",pid);
	
	spin_lock(&my_lock);
	list_for_each_safe(pos, q, &ret_queue->list){
		tmp = list_entry(pos, struct queue, list);
		copy_to_user(kpd, &(tmp->data), sizeof(struct ku_pir_data));
		//printk("read data from user is %ld, %c\n", (tmp->data).timestamp, (tmp->data).rf_flag);
		list_del(pos);
		kfree(tmp);
		break;
		
	}
	spin_unlock(&my_lock);
}

int insert_from_user(struct ku_pir_data* kpd){
	
	// insert data from user (insertData func in lib) // copy_from_user 	

	struct queue_list *tmp = 0;
	struct queue *tar = 0;
	int ret;
	int ret_final = 0;
	
//	if(is_not_exist(df->pid)) return -1;
	
	rcu_read_lock();
	list_for_each_entry_rcu(tmp, &my_queues.list, list){
		tar = (struct queue*)kmalloc(sizeof(struct queue), GFP_KERNEL);
		ret = copy_from_user(&(tar->data), kpd, sizeof(struct ku_pir_data));
		if(is_full(tmp->pid)) remove_old_queue(tmp->pid);		// if data is full remove
			
		if(ret != 0) ret_final = -1;
		spin_lock(&my_lock);
		list_add_tail_rcu(&tar->list, &tmp->q.list);
		spin_unlock(&my_lock);
		
		
	}
	rcu_read_unlock();
	wake_up_interruptible(&my_wq);			// wake up every blocking processes
	
	return ret_final;
		
}

static irqreturn_t ku_pir_isr(int irq, void* dev_id){
	
	// isr func for pir sensor
  
	char rf_flag;
	long unsigned int timestamp;

	timestamp = jiffies;
	if(gpio_get_value(KUPIR_SENSOR) == 1){
		rf_flag = '0';				// 1 -> 0
	} else{
		rf_flag = '1';				// 0 -> 1
	}

	insert_from_isr(timestamp, rf_flag);

	return IRQ_HANDLED;
}


static long ku_pir_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	
	long ret = 0L;
		
	switch(cmd){
	case KU_PIR_INSERTDATA :
	{
		struct ku_pir_data* kpd;
		kpd = (struct ku_pir_data*)arg;
		ret = insert_from_user(kpd);
		break;
	}
	case KU_PIR_READ :
	{
		struct ku_pir_data * kpd;
		kpd = (struct ku_pir_data*)arg;
		ku_pir_read(kpd);
		break;
	}
	case KU_PIR_CLOSE :
		ret = remove_queue_list();
		break;
	case KU_PIR_FLUSH :
		ret = remove_queue();
		break;
	case KU_PIR_INIT :
		ret = init_queue();
		break;			
	
	}

	return ret;
}

struct file_operations ku_pir_fops =
{
	.unlocked_ioctl = ku_pir_ioctl,
};


static int __init ku_pir_init(void){

	printk("INIT MODULE\n");
	
	alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &ku_pir_fops);
	cdev_add(cd_cdev, dev_num, 1);


	INIT_LIST_HEAD(&my_queues.list);	// init my_queues

	spin_lock_init(&my_lock);
	init_waitqueue_head(&my_wq);
	gpio_request_one(KUPIR_SENSOR, GPIOF_IN, "sensor1");
	irq_num = gpio_to_irq(KUPIR_SENSOR);
	request_irq(irq_num, ku_pir_isr, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "sensor_irq",NULL);
	return 0;
}

static void __exit ku_pir_exit(void){
	
	struct queue_list *tmp;
	struct list_head *pos = 0;
	struct list_head *q = 0;
	
	list_for_each_safe(pos, q, &my_queues.list){
		tmp = list_entry(pos, struct queue_list, list);
		list_del(pos);
		kfree(tmp);	
	}
	printk("EXIT MODULE\n");

	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num, 1);
	disable_irq(irq_num);
	free_irq(irq_num, NULL);
	gpio_free(KUPIR_SENSOR);
}


module_init(ku_pir_init);
module_exit(ku_pir_exit);

