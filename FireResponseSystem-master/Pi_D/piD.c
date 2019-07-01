#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>
#include "MQTTClient.h"


#define ADDRESS "tcp://172.20.10.7:1883"
#define ADDRESS_PI_A "tcp://172.20.10.8:1883"
#define ADDRESS_PI_B "tcp://172.20.10.9:1883"

#define CLIENTID_PI_A "Client_pi_a"
#define CLIENTID_PI_B "Client_pi_b"
#define CLIENTID_PI_S "Client_pi_s"
#define CLIENTID "Client_Receiver"
#define TOPIC_A2D "A2D"
#define TOPIC_B2D "B2D"
#define TOPIC_C2D "C2D"
#define TOPIC_D2A "D2A"
#define TOPIC_D2B "D2B"
#define TOPIC_S2D "S2D"
#define TOPIC_D2S "D2S"
#define QOS 1
#define TIMEOUT 100L

#define STATUS_NORMAL 0 // when wait for fire detecting
#define STATUS_FIRE_DETECTED 1 // when fire detected
#define STATUS_WAIT_DOOR 2 // when wait A,B for receive person number
volatile MQTTClient_deliveryToken deliveredtoken;
MQTTClient client_to_A;
MQTTClient client_to_B;
MQTTClient client_to_S;
int state = STATUS_NORMAL;
int received_by_a = 0;
int received_by_b = 0;

int received_req_a = 0;
int received_req_b = 0;

int dev;
void delivered(void *context, MQTTClient_deliveryToken dt)
{
	printf("Message with token value %d delivery confirmed\n",dt);
	deliveredtoken = dt;
}

void connlost(void *context,char *cause)
{
	printf("\nConnection lost\n");
	printf("cause:%s\n",cause);
}

typedef struct pi_C_data{
        char temperature[5];
        char humidity[5];
        char room_num[2];
        char is_fire[2];

} C_data;

typedef struct Room{
	char room_num[10];
	char person_num[10];
	char is_fire[10];
} Room;

// global variable for using send  A,B<->D<->Station
Room Room_info[3];

void display(C_data *room_0,C_data *room_1,C_data *room_2){
//display temperature, humidity, is_fire, room_num data from pi C
	char title[4096] = 
"\
+---------------------------------+     +---------------------------------+     +---------------------------------+\n\
|             Room %s              |     |             Room %s              |     |             Room %s              |\n\
|---------------------------------|     |---------------------------------|     |---------------------------------|\n\
|                                 |     |                                 |     |                                 |\n\
|        Temperature : %4s       |     |        Temperature : %4s       |     |        Temperature : %4s       |\n\
|                                 |     |                                 |     |                                 |\n\
|         Humidity  : %4s        |     |         Humidity  : %4s        |     |         Humidity  : %4s        |\n\
|                                 |     |                                 |     |                                 |\n\
|        Fire Detected : %s        |     |        Fire Detected : %s        |     |        Fire Detected : %s        |\n\
|                                 |     |                                 |     |                                 |\n\
+---------------------------------+     +---------------------------------+     +---------------------------------+\n";
	
printf(title,room_0->room_num,room_1->room_num,room_2->room_num,room_0->temperature,room_1->temperature,room_2->temperature,room_0->humidity,room_1->humidity,room_2->humidity,room_0->is_fire,room_1->is_fire,room_2->is_fire);

}

void parse_door_data(char *data, char *person_num, char *room_num){
	char *ptr = strtok(data,",");
	strcpy(person_num,ptr);
	ptr = strtok(NULL,",");
	strcpy(room_num,ptr);

}

void LED_SPEAKER_ON(){
	dev = open("/dev/fire_detect_dev", O_RDONLY);

}

void parse_C_data(char *data, C_data *room){
	char *ptr = strtok(data,",");
	int i;
	for(i=0;i<3;i++){
		strcpy(((room+i)->temperature),ptr);
		ptr = strtok(NULL,",");
		strcpy((room+i)->humidity,ptr);
		ptr = strtok(NULL,",");
		strcpy((room+i)->room_num,ptr);
		ptr = strtok(NULL,",");
		strcpy((room+i)->is_fire,ptr);
		ptr = strtok(NULL,",");
	}

}

void send_to_A(char *data){
	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	MQTTClient_deliveryToken token;
	pubmsg.payload = data;
	pubmsg.payloadlen = strlen(data);
	pubmsg.qos = QOS;
	pubmsg.retained = 0;
	MQTTClient_publishMessage(client_to_A,TOPIC_D2A,&pubmsg,&token);
	MQTTClient_waitForCompletion(client_to_A,token,TIMEOUT);
	printf("Send to A with delivery token %d \n",token);
}

void send_to_B(char *data){
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        pubmsg.payload = data;
        pubmsg.payloadlen = strlen(data);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        MQTTClient_publishMessage(client_to_B,TOPIC_D2B,&pubmsg,&token);
        MQTTClient_waitForCompletion(client_to_B,token,TIMEOUT);
        printf("Send to B with delivery token %d \n",token);

}

void send_to_S(char *data){
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        pubmsg.payload = data;
        pubmsg.payloadlen = strlen(data);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        MQTTClient_publishMessage(client_to_S,TOPIC_D2S,&pubmsg,&token);
        MQTTClient_waitForCompletion(client_to_S,token,TIMEOUT);
        printf("Send to Station with delivery token %d \n",token);
}

int isfired_anywhere(){
	int result = 0;
	int i;
	for(i=0;i<3;i++){
		if(!strcmp(Room_info[i].is_fire,"O")){
		result = 1;
		break;
		}
	}
	return result;
}


int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
	C_data room[3];
	char msg[4096];
        char* received_data;
	int i;
	received_data = message->payload;
	received_data[message->payloadlen] = '\0';
	if(!strcmp(topicName,"A2D")){ //received data by A
		if(state == STATUS_FIRE_DETECTED ){
			parse_door_data(received_data,Room_info[0].person_num,Room_info[0].room_num);
			printf("person_num : %s\n",Room_info[0].person_num);
                	printf("room_num : %s\n",Room_info[0].room_num);
			printf("Received by A!!!!!\n");
			received_by_a = 1;
			if(received_by_a == 1 && received_by_b == 1){
				sprintf(msg,"%s,%s,%s,%s,%s,%s,%s,%s,%s",Room_info[0].is_fire,Room_info[0].room_num,Room_info[0].person_num,Room_info[1].is_fire,Room_info[1].room_num,Room_info[1].person_num,Room_info[2].is_fire,Room_info[2].room_num,"?");
				send_to_S(msg);
				received_by_a = 0;
				received_by_b = 0;
				state = STATUS_WAIT_DOOR;
			}
		}
		else if(state == STATUS_WAIT_DOOR){
			parse_door_data(received_data,Room_info[0].person_num,Room_info[0].room_num);
			received_req_a = 1;
			if(received_req_a == 1 && received_req_b == 1){
				sprintf(msg,"%s,%s,%s,%s,%s,%s",Room_info[0].room_num,Room_info[0].person_num,Room_info[1].room_num,Room_info[1].person_num,Room_info[2].room_num,"?");
				send_to_S(msg);
				received_req_a = 0;
				received_req_b = 0;
			}
		}
	}
	else if(!strcmp(topicName,"B2D")){ // received data by B
		if(state == STATUS_FIRE_DETECTED ){
                        parse_door_data(received_data,Room_info[1].person_num,Room_info[1].room_num);
                        printf("person_num : %s\n",Room_info[1].person_num);
                        printf("room_num : %s\n",Room_info[1].room_num);
                        printf("Received by B!!!!!\n");
			received_by_b = 1;
			if(received_by_a == 1 && received_by_b == 1){
				sprintf(msg,"%s,%s,%s,%s,%s,%s,%s,%s,%s",Room_info[0].is_fire,Room_info[0].room_num,Room_info[0].person_num,Room_info[1].is_fire,Room_info[1].room_num,Room_info[1].person_num,Room_info[2].is_fire,Room_info[2].room_num,"?");
				send_to_S(msg);
				received_by_a = 0;
				received_by_b = 0;
				state = STATUS_WAIT_DOOR;
			}
                }
                else if(state == STATUS_WAIT_DOOR){
                        parse_door_data(received_data,Room_info[1].person_num,Room_info[1].room_num);
			received_req_b = 1;
			if(received_req_a == 1 && received_req_b == 1){
				sprintf(msg,"%s,%s,%s,%s,%s,%s",Room_info[0].room_num,Room_info[0].person_num,Room_info[1].room_num,Room_info[1].person_num,Room_info[2].room_num,"?");
				send_to_S(msg);
				received_req_a = 0;
				received_req_b = 0;
			}
                }
	}
	else if(!strcmp(topicName,"C2D")){ // received data by C
		if(state == STATUS_NORMAL){
			parse_C_data(received_data,room);
			display(&room[0],&room[1],&room[2]);
			for(i=0;i<3;i++){
				strcpy(Room_info[i].is_fire,room[i].is_fire);
				strcpy(Room_info[i].room_num,room[i].room_num);
			}
			if(isfired_anywhere()){
				LED_SPEAKER_ON();
				send_to_A("fire,true");
				send_to_B("fire,true");
				state = STATUS_FIRE_DETECTED;
			}
		}

	}
	else if(!strcmp(topicName,"S2D")){ // received data by Station
		if(!strcmp(received_data,"request")) // if receive cmd from fire station to get Room_info
		{
			send_to_A("request");
			send_to_B("request");
		}

	}
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
}



int main(void){
	int i=0;
	MQTTClient client;
	//MQTTClient client_to_A;
	//MQTTClient client_to_B;
	//MQTTClient client_to_S;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	MQTTClient_connectOptions conn_opts_to_A = MQTTClient_connectOptions_initializer;
	MQTTClient_connectOptions conn_opts_to_B = MQTTClient_connectOptions_initializer; 
	MQTTClient_connectOptions conn_opts_to_S = MQTTClient_connectOptions_initializer;

	int rc;
	int rc_to_A;
	int rc_to_B;
	int rc_to_S;

	// make MQTT client for receiver,and sender for A,B,station -> total 4
	MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTClient_create(&client_to_A,ADDRESS_PI_A, CLIENTID_PI_A, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTClient_create(&client_to_B,ADDRESS_PI_B, CLIENTID_PI_B, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTClient_create(&client_to_S,ADDRESS, CLIENTID_PI_S, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	// setting connect options
	conn_opts.keepAliveInterval = 20;
	conn_opts_to_A.keepAliveInterval = 20;
	conn_opts_to_B.keepAliveInterval = 20;
	conn_opts_to_S.keepAliveInterval = 20;

	conn_opts.cleansession = 1;
	conn_opts_to_A.cleansession = 1;
	conn_opts_to_B.cleansession = 1;
	conn_opts_to_S.cleansession = 1;
	// setting MQTT receive callback func to msgarrvd
	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

	// MQTT connect
		// for receiver
	if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    	{
        	printf("Failed to connect , return code %d\n", rc);
        	exit(-1);
    	}

		// for sender to A
	if ((rc_to_A = MQTTClient_connect(client_to_A, &conn_opts_to_A)) != MQTTCLIENT_SUCCESS)
	{
		printf("Failed to connect for send to A , return code %d\n", rc_to_A);
		exit(-1);
	}

		// for sender to B
	if ((rc_to_B = MQTTClient_connect(client_to_B, &conn_opts_to_B)) != MQTTCLIENT_SUCCESS)
        {
                printf("Failed to connect for send to B , return code %d\n", rc_to_B);
                exit(-1);
        }

		// for sender to S
	if ((rc_to_S = MQTTClient_connect(client_to_S, &conn_opts_to_S)) != MQTTCLIENT_SUCCESS)
	{
		printf("Failed to connect for send to S , return code %d\n", rc_to_S);
		exit(-1);
	}

	// init room_info struct
	for(i=0;i<3;i++){
		strcpy(Room_info[i].room_num,"-1");
		strcpy(Room_info[i].person_num,"-1");
		strcpy(Room_info[i].is_fire,"X");
	}


	// receive data from A,B,C
	MQTTClient_subscribe(client,TOPIC_A2D,QOS);
	MQTTClient_subscribe(client,TOPIC_B2D,QOS);
	MQTTClient_subscribe(client,TOPIC_C2D,QOS);
	MQTTClient_subscribe(client,TOPIC_S2D,QOS);
	//send_to_S("O,0,3,X,1,5,X,2,10");
	//LED_SPEAKER_ON();



	getchar();
	close(dev);
}


