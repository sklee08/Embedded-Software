#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <string.h>
#include "MQTTClient.h"


#define ADDRESS "tcp://172.20.10.7:1883"

#define CLIENTID_PI_D "Client_pi_d"
#define CLIENTID "Client_Station"
#define TOPIC_S2D "S2D"
#define TOPIC_D2S "D2S"
#define QOS 1
#define TIMEOUT 100L

volatile MQTTClient_deliveryToken deliveredtoken;
MQTTClient client_to_D;
int is_fired_anywhere = 0;

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

typedef struct Room{
	char room_num[10];
	char person_num[10];
	char is_fire[10];
} Room;

// global variable for using send  A,B<->D<->Station
Room Room_info[3];

void parse_room_fire_detected(char *data, Room *room){
	char *ptr = strtok(data,",");
        int i;
        for(i=0;i<3;i++){
                strcpy(((room+i)->is_fire),ptr);
                ptr = strtok(NULL,",");
                strcpy((room+i)->room_num,ptr);
                ptr = strtok(NULL,",");
                strcpy((room+i)->person_num,ptr);
                ptr = strtok(NULL,",");
        }

}

void parse_room_request(char *data, Room *room){
	char *ptr = strtok(data,",");
	int i;
	for(i=0;i<3;i++){
                strcpy((room+i)->room_num,ptr);
                ptr = strtok(NULL,",");
                strcpy((room+i)->person_num,ptr);
                ptr = strtok(NULL,",");
	}

}

void send_to_D(char *data){
        int rc;
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        pubmsg.payload = data;
        pubmsg.payloadlen = strlen(data);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        MQTTClient_publishMessage(client_to_D,TOPIC_S2D,&pubmsg,&token);
        MQTTClient_waitForCompletion(client_to_D,token,TIMEOUT);
        printf("Send to Station with delivery token %d \n",token);
}


void display(Room *room_0,Room *room_1,Room *room_2){
//display temperature, humidity, is_fire, room_num data from pi C
	char title[4096] = 
"\
+---------------------------------+     +---------------------------------+     +---------------------------------+\n\
|             Room %s              |     |             Room %s              |     |             Room %s              |\n\
|---------------------------------|     |---------------------------------|     |---------------------------------|\n\
|                                 |     |                                 |     |                                 |\n\
|        Person num : %4s        |     |        Person num : %4s        |     |        Person num : %4s        |\n\
|                                 |     |                                 |     |                                 |\n\
|        Fire Detected : %s        |     |        Fire Detected : %s        |     |        Fire Detected : %s        |\n\
|                                 |     |                                 |     |                                 |\n\
+---------------------------------+     +---------------------------------+     +---------------------------------+\n";

printf(title,room_0->room_num,room_1->room_num,room_2->room_num,room_0->person_num,room_1->person_num,room_2->person_num,room_0->is_fire,room_1->is_fire,room_2->is_fire);

}

void print_fire_detected(){
	char detected[4096] = 
"\
\033[0;31m!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\
!!!!!!!!!         FIRE DETECTED      !!!!!!!!!!!!\n\
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\033[0m";
printf(detected);
}

void menu(){
int choice;

printf("1.Request for update each room info\n");
printf("2.Fire finished\n");
scanf("%d",&choice);

switch(choice){
	case 1:
		send_to_D("request");
		break;
	case 2:
		is_fired_anywhere = 0;
		break;
	default:
		break;
}
}



int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
	
	char msg[4096];
        char* received_data;
	int i;
	received_data = message->payload;
	received_data[message->payloadlen] = '\0';

	if(!strcmp(topicName,"D2S")){ // received data by pi D
		//fire detected
		if(received_data[0] == 'O' || received_data[0] == 'X'){
			parse_room_fire_detected(received_data,Room_info);
			print_fire_detected();
			display(&Room_info[0],&Room_info[1],&Room_info[2]);
			is_fired_anywhere = 1;

		}
		else{
			parse_room_request(received_data,Room_info);
			display(&Room_info[0],&Room_info[1],&Room_info[2]);

		}

	}
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
}



int main(void){
	int i=0;
	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	MQTTClient_connectOptions conn_opts_to_D = MQTTClient_connectOptions_initializer;

	int rc;
	int rc_to_D;

	// make MQTT client for receiver,and sender to pi D
	MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTClient_create(&client_to_D,ADDRESS, CLIENTID_PI_D, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	// setting connect options
	conn_opts.keepAliveInterval = 20;
	conn_opts_to_D.keepAliveInterval = 20;
	conn_opts.cleansession = 1;
	conn_opts_to_D.cleansession = 1;
	// setting MQTT receive callback func to msgarrvd
	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

	// MQTT connect
		// for receiver
	if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    	{
        	printf("Failed to connect , return code %d\n", rc);
        	exit(EXIT_FAILURE);
    	}
		// for sender to D
	if ((rc_to_D = MQTTClient_connect(client_to_D, &conn_opts_to_D)) != MQTTCLIENT_SUCCESS)
	{
		printf("Failed to connect for send to D , return code %d\n", rc_to_D);
		exit(EXIT_FAILURE);
	}

	// init room_info struct
	for(i=0;i<3;i++){
		strcpy(Room_info[i].room_num,"-1");
		strcpy(Room_info[i].person_num,"?");
		strcpy(Room_info[i].is_fire,"X");
	}


	// receive data from A,B,C
	MQTTClient_subscribe(client,TOPIC_D2S,QOS);
	
	while(1){
		if(is_fired_anywhere == 1){
			menu();

		}


	}
	getchar();

}
