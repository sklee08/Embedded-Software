#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>

#include <linux/delay.h>
#include <linux/ktime.h>

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/rculist.h>
#include <linux/jiffies.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/unistd.h>
#include <linux/workqueue.h>

#include "door.h"

MODULE_LICENSE("GPL");

#define DEV_NAME "door_dev"

#define SENSOR 17
#define TRIG1 27
#define ECHO1 22
#define TRIG2 23
#define ECHO2 24

#define PIN1 6
#define PIN2 13
#define PIN3 19
#define PIN4 26

#define STEPS 8

//  출입 비교 코드 수행 안됨

// 스레드, 워크큐
struct task_struct *test_task1 = NULL;
struct task_struct *test_task2 = NULL;
struct task_struct *test_task3 = NULL;
static struct workqueue_struct *test_wq;

typedef struct{
    struct work_struct my_work;
    struct sensor_data data;
}my_work_t;

my_work_t *work;

// 방에 있는 사람수
int people;

// 자료구조
struct sensor_list{
    struct list_head slist;
    struct sensor_data data;
    int count;
};

struct sensor_list pirList;    // pir1 큐
struct sensor_list ultraList1;    // 초음파1 큐
struct sensor_list ultraList2;   // 초음파2 큐

// 초음파 센서 플래그
int u1_flag;
int u2_flag;

int isFired;    // 화재 여부 플래그 (1이면 화재 발생)  
int isOpen;    // 현재 문 상태 저장(0:close, 1:open)
unsigned long irqMask;  // 인터럽트 dis/en Mask

spinlock_t my_lock; // rcu write용

static void insert_data(struct sensor_list *list, struct sensor_data *data){
    struct sensor_list *tmp = 0;
    struct list_head *pos = 0;
    struct list_head *n = 0;


    // 유휴공간 없을 경우
    if(list->count == MAX_DATA){
        // 가장 오래된 데이터(맨 앞) 삭제
        spin_lock(&my_lock);
        list_for_each_safe(pos, n, &list->slist){
            list_del_rcu(pos);
            break;
        }
        list->count -= 1;
        spin_unlock(&my_lock);
    }

    // 데이터 노드 생성
    tmp = (struct sensor_list*)kmalloc(sizeof(struct sensor_list), GFP_KERNEL);
    tmp->data.timestamp = data->timestamp;
    tmp->data.rf_flag = data->rf_flag;

    // 데이터 노드 추가
    spin_lock(&my_lock);
    list_add_tail_rcu(&tmp->slist, &list->slist);
    list->count += 1;
    spin_unlock(&my_lock);
}

static int read_data(struct sensor_list *list, struct sensor_data *data){
    struct sensor_list *tmp = 0;
    struct list_head *pos = 0;
    struct list_head *n = 0;
    
    // 큐에 데이터가 없는 경우
    if(list_empty(&list->slist)){
        return 0;
    }

    // 데이터가 있는 경우, 맨 처음 데이터 읽은 후 제거
    spin_lock(&my_lock);
    list_for_each_safe(pos, n, &list->slist){
        tmp = list_entry(pos, struct sensor_list, slist);

        data->timestamp = tmp->data.timestamp;
        data->rf_flag = tmp->data.rf_flag;
        
        list_del_rcu(pos);
        kfree(tmp);   

        list->count -= 1;
        break;
    }
    spin_unlock(&my_lock);

    return 1;
}

static int read_tail_data(struct sensor_list *list, struct sensor_data *data){
    struct sensor_list *tmp = 0;
    struct list_head *pos = 0;
    struct list_head *n = 0;
    
    // 큐에 데이터가 없는 경우
    if(list_empty(&list->slist)){
        return 0;
    }

    // 데이터가 있는 경우, 맨 마지막 데이터 읽은 후 제거
    spin_lock(&my_lock);
    list_for_each_safe(pos, n, &list->slist){
        // 마지막 데이터 가져오기
        if(list_entry(n, struct sensor_list, slist) == NULL){
            tmp = list_entry(pos, struct sensor_list, slist);
            data->timestamp = tmp->data.timestamp;
            data->rf_flag = tmp->data.rf_flag;
            list_del_rcu(pos);
            kfree(tmp);   
            list->count -= 1;
        }     
    }
    spin_unlock(&my_lock);

    return 1;
}

static void flush_data(struct sensor_list *list){
    struct sensor_list *tmp = 0;
    struct list_head *pos = 0;
    struct list_head *n = 0;

    spin_lock(&my_lock);
    list_for_each_safe(pos, n, &list->slist){
        tmp = list_entry(pos, struct sensor_list, slist);

        list_del_rcu(pos);
        kfree(tmp);
    }
    list->count = 0;
    spin_unlock(&my_lock);
}

void forward(int round, int delay){
	int pin[8] = {PIN2, PIN1, PIN3, PIN2, PIN4, PIN3, PIN1, PIN4};

	int i=0, j=0;

	for(i=0; i<round; i++){
		for(j=0; j<STEPS; j++){
			if(j % 2 == 0)
				gpio_set_value(pin[j], 1);
			else
				gpio_set_value(pin[j], 0);
			udelay(delay);
		}
	}
}

void backward(int round, int delay){
	int pin[8] = {PIN2, PIN1, PIN3, PIN2, PIN4, PIN3, PIN1, PIN4};

	int i=0, j=0;

	for(i=0; i<round; i++){
		for(j=STEPS-1; j>=0; j--){
			if(j % 2 == 0)
				gpio_set_value(pin[j], 1);
			else
				gpio_set_value(pin[j], 0);
			udelay(delay);
		}
	}
}

void moveDegree(int degree, int delay, int direction){
	int tmp = degree * 142;
	if(direction == 0)
		forward(tmp/100, delay);
	else
		backward(tmp/100, delay);
}


// backward() 이용
static void closeDoor(void){
    	moveDegree(90,1200, 1);
	isOpen = 1;
      
}

// forward() 이용
static void openDoor(void){
   moveDegree(90,1200, 0);  
	isOpen = 0;
}

// 초음파 센서1 스레드
static int chkDist1(void *data){
    ktime_t start, end;
    s64 actual_time;
    int distance ;
    struct sensor_data value, ret;
    ktime_t tmp;

    while(!kthread_should_stop()){
        if(gpio_get_value(SENSOR) == 1){
            //printk("초음파 센서1 실행중 \n");
            //msleep(500);
            if(u1_flag > u2_flag) {
                msleep(1);
                continue;
            }
            
            gpio_direction_output(TRIG1, 0);
            gpio_direction_input(ECHO1);
        
            gpio_set_value(TRIG1, 0);

            mdelay(50);

            gpio_set_value(TRIG1, 1);

            udelay(10);

            gpio_set_value(TRIG1, 0);

            while(gpio_get_value(ECHO1) == 0) ;
            start = ktime_get();

            while(gpio_get_value(ECHO1) == 1) ;
            end = ktime_get();

            actual_time = ktime_to_us(ktime_sub(end, start));
            //printk("Time taken for func(): %lld\n", (long long)actual_time);
        
            // 마이크로세컨드
            //distance = actual_time / 58 ;

            // 큐에 추가할 데이터
            tmp = ktime_get();
            value.timestamp = ktime_to_ms(tmp);
            // 기준거리보다 작을 경우 지나감 (시간으로 표현)
            if((long long)actual_time <800){
                value.rf_flag = 1;
                insert_data(&ultraList1, &value);
                u1_flag++;

                printk("u1_flag: %d\n", u1_flag);
		msleep(1000);
            }  
                
            else{
                /*
                value.rf_flag = 0;
                insert_data(&ultraList1, &value);
                printk("1 안지나감\n");
                */
            }               
        } else{
            //msleep(10); 
        }  
        msleep(1); 
    }
}
// 초음파 센서2 스레드
static int chkDist2(void *data){
    
    ktime_t start, end;
    s64 actual_time;
    int distance ;
    struct sensor_data value, ret;
    ktime_t tmp;

    while(!kthread_should_stop()){
        if(gpio_get_value(SENSOR) == 1){
            //printk("초음파 센서2 실행중 \n");
            //msleep(500);
            if(u2_flag > u1_flag) {
                msleep(1);
                continue;
            }
            
            gpio_direction_output(TRIG2, 1);
            gpio_direction_input(ECHO2);
        
            gpio_set_value(TRIG2, 0);

            mdelay(50);

            gpio_set_value(TRIG2, 1);

            udelay(10);

            gpio_set_value(TRIG2, 0);

            while(gpio_get_value(ECHO2) == 0) ;
            start = ktime_get();

            while(gpio_get_value(ECHO2) == 1) ;
            end = ktime_get();

            actual_time = ktime_to_us(ktime_sub(end, start));
            //printk("Time taken for func(): %lld\n", (long long)actual_time);

            // 큐에 추가할 데이터
            tmp = ktime_get();
            value.timestamp = ktime_to_ms(tmp);

            // 기준거리보다 작을 경우 지나감 (시간으로 표현)
            if((long long)actual_time < 800){
                value.rf_flag = 1;
                insert_data(&ultraList2, &value);
                u2_flag++;

                printk("u2_flag: %d\n", u2_flag);
		        msleep(1000);
            }
                
            else{
                /*
                value.rf_flag = 0;
                insert_data(&ultraList2, &value);
                printk("2 안지나감\n");
                */
            }     
        } else{
            //msleep(10); 
        }
        msleep(1);
    }
}

// 인원수 체크 워크큐
static void chkDirection(struct work_struct *work){
    struct sensor_data value, pir1, pir2, ret1, ret2;   // rising, falling, ultra1, ultra2
    int pass, min;


    // 같아야 정상
    if(u1_flag == u2_flag){
        min = u1_flag;
        for(pass=0; pass<min; pass++){
            read_data(&ultraList1, &ret1);
            read_data(&ultraList2, &ret2);
            u1_flag--;
            u2_flag--;

            if(ret1.timestamp < ret2.timestamp) {
                printk("사람 추가\n");
                people++;
            }
            else if(ret2.timestamp < ret1.timestamp) {
                printk("사람 제거\n");
                people--;
            }
		
		if(isFired == 1){
        if(people == 0) {
            if(isOpen == 1) {
               local_irq_save(irqMask);
                closeDoor();     
               local_irq_restore(irqMask);
            }
        }
        else {
            if(isOpen == 0) {
              local_irq_save(irqMask);
                openDoor();     
              local_irq_restore(irqMask);
            }      
        }
        }
    }
    
  }

    // 만약 다르면..?
/*    exit

    else{        
        flush_data(&ultraList1);
        flush_data(&ultraList2);
        flush_data(&pirList);
        u1_flag = 0;
        u2_flag = 0;        
    }
*/    
    //if(u1_flag < u2_flag) min = u1_flag;
    //else min = u2_flag; 

    //return 0;
}

static int readData(void){
    return people;
}

static void detectFire(void){
    isFired = 1;
printk("people : %d\n",people);
printk("isopen : %d\n",isOpen);
    if(people == 0) {
        if(isOpen == 1) {
            local_irq_save(irqMask);
            closeDoor();     
            local_irq_restore(irqMask);
        }
    }
    else {
        if(isOpen == 0) {
          local_irq_save(irqMask);
            openDoor();     
          local_irq_restore(irqMask);
        }      
    }       
}

static long simple_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
	switch(cmd){
		case SIMPLE_IOCTL1:
			return readData();				
			
		case SIMPLE_IOCTL2:
			openDoor();
            break;

		case SIMPLE_IOCTL3:
			closeDoor();
            break;

        case SIMPLE_IOCTL4:
			detectFire();
            break;

		default:
			return -1;
					
	}
}

// pir 센서
static int irq_num;


static int simple_sensor_open(struct inode *inode, struct file *file){
    enable_irq(irq_num);
	return 0;
}

static int simple_sensor_release(struct inode *inode, struct file* file){
	disable_irq(irq_num);
	return 0;
}

struct file_operations simple_sensor_fops=
{
    .unlocked_ioctl = simple_ioctl,
	.open = simple_sensor_open,
	.release = simple_sensor_release
};

static irqreturn_t simple_sensor_isr(int irq, void* dev_id){
    struct sensor_data value;
    ktime_t uStart, uEnd;
    ktime_t tmp;
    int gpio_flag = gpio_get_value(SENSOR);

    printk("detect 발생: %d\n", gpio_flag);
    tmp = ktime_get();
    value.timestamp = ktime_to_ms(tmp);

    // 초음파 센서 실행 & 시간 저장
    if(gpio_flag == 1){
        //kthread_run(chkDist1, NULL, "my_thread1");
        //kthread_run(chkDist2, NULL, "my_thread2");

        value.rf_flag = 1;
        insert_data(&pirList, &value);
    }
    // 초음파 센서 중지 & 시간 비교
    else if(gpio_flag == 0){
        //kthread_stop(test_task1);
        //kthread_stop(test_task2);

        value.rf_flag = 0;
        insert_data(&pirList, &value);

        if(test_wq){
            work = (my_work_t*)kmalloc(sizeof(my_work_t), GFP_KERNEL);
            if(work){
                INIT_WORK((struct work_struct*)work, chkDirection);
                queue_work(test_wq, (struct work_struct*)work);
            }
        }
    }

	return IRQ_HANDLED;
}

static dev_t dev_num;
static struct cdev *cd_cdev;

static int __init simple_sensor_init(void){
	int ret;

	printk("Init Module\n");

    people = 0;
    u1_flag = 0;
    u2_flag = 0;

    isFired=0;
    isOpen=1;

	// allocate character device
	alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
	cd_cdev = cdev_alloc();
	cdev_init(cd_cdev, &simple_sensor_fops);
	cdev_add(cd_cdev, dev_num, 1);

    // 데이터 큐
    INIT_LIST_HEAD(&pirList.slist);
    INIT_LIST_HEAD(&ultraList1.slist);
    INIT_LIST_HEAD(&ultraList2.slist);

    // 모터
    gpio_request_one(PIN1, GPIOF_OUT_INIT_LOW, "p1");
    gpio_request_one(PIN2, GPIOF_OUT_INIT_LOW, "p2");
    gpio_request_one(PIN3, GPIOF_OUT_INIT_LOW, "p3");
    gpio_request_one(PIN4, GPIOF_OUT_INIT_LOW, "p4");
	gpio_set_value(PIN1, 1);


    
    // 스레드, 워크큐
    test_wq = create_workqueue("test_workqueue");

    // 초음파 센서
    gpio_request(TRIG1, "TRIG1");
    gpio_request(ECHO1, "ECHO1");
    gpio_request(TRIG2, "TRIG2");
    gpio_request(ECHO2, "ECHO2");


	// request GPIO and interrupt handler
	gpio_request_one(SENSOR, GPIOF_IN, "sensor");
	irq_num = gpio_to_irq(SENSOR);

	ret  = request_irq(irq_num, simple_sensor_isr, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "sensor_irq", NULL);
    if(ret){
		printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
		free_irq(irq_num, NULL);
	}
	else{
		disable_irq(irq_num);
	}

gpio_set_value(SENSOR, 0);
test_task1 = kthread_create(chkDist1, NULL, "my_thread1");
    test_task2 = kthread_create(chkDist2, NULL, "my_thread2");
    //test_task3 = kthread_create(chkDirection, NULL, "my_thread3");
    if(IS_ERR(test_task1)){
        test_task1 = NULL;
        printk("test kernel thread1 ERROR\n");
    }  
    if(IS_ERR(test_task2)){
        test_task2 = NULL;
        printk("test kernel thread2 ERROR\n");
    }
wake_up_process(test_task1);
    wake_up_process(test_task2);


	return 0;
}

static void __exit simple_sensor_exit(void){
    struct sensor_list *tmp = 0;
    struct list_head *pos = 0;
    struct list_head *q = 0;

    list_for_each_safe(pos, q, &pirList.slist) {
        tmp = list_entry(pos, struct sensor_list, slist);
        list_del_rcu(pos);
        kfree(tmp);
    }
    list_for_each_safe(pos, q, &ultraList1.slist) {
        tmp = list_entry(pos, struct sensor_list, slist);
        list_del_rcu(pos);
        kfree(tmp);
    }
    list_for_each_safe(pos, q, &ultraList2.slist) {
        tmp = list_entry(pos, struct sensor_list, slist);
        list_del_rcu(pos);
        kfree(tmp);
    }

    people=0;
    u1_flag=0;
    u2_flag=0;

    isFired=0;
    isOpen=1;

	printk("Exit Module \n");
	cdev_del(cd_cdev);
	unregister_chrdev_region(dev_num, 1);

	free_irq(irq_num, NULL);
    gpio_set_value(SENSOR, 0);
	gpio_free(SENSOR);

    gpio_set_value(TRIG1, 0);
    gpio_set_value(ECHO1, 0);
	gpio_free(TRIG1);
	gpio_free(ECHO1);

    gpio_set_value(TRIG2, 0);
    gpio_set_value(ECHO2, 0);
	gpio_free(TRIG2);
	gpio_free(ECHO2);

    gpio_free(PIN1);
    gpio_free(PIN2);
    gpio_free(PIN3);
    gpio_free(PIN4);

    if(test_task1){
        kthread_stop(test_task1);
        printk("test kernel thread1 STOP\n");
    }
    if(test_task2){
        kthread_stop(test_task2);
        printk("test kernel thread2 STOP\n");
    }

    
    flush_workqueue(test_wq);
    destroy_workqueue(test_wq);
}

module_init(simple_sensor_init);
module_exit(simple_sensor_exit);
