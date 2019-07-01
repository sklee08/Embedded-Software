#include "stdio.h"
#include <stdlib.h>
#include "string.h"
#include "MQTTClient.h"
#include <unistd.h>
#include "door_lib.c"

#define ADDRESS     "tcp://172.20.10.7:1883"
#define ADDRESS_FOR_RECEIVE     "tcp://172.20.10.9:1883"
#define CLIENTID_SENDER    "Client_Sender_B"
#define CLIENTID_RECEIVER    "Client_Receiver_B"
#define TOPIC_B2D       "B2D"
#define TOPIC_D2B	"D2B"
// #define PAYLOAD     "Message By C"
#define QOS         1
#define TIMEOUT     100L

int ret_from_callback = 0;
volatile MQTTClient_deliveryToken deliveredtoken;

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



int receiver(void * context, char* topicName, int topicLen, MQTTClient_message *message){
	
	char* received_data;
	printf("Msg arrived!\n");
	

	received_data = message->payload;	
	received_data[message->payloadlen] = '\0';
	printf("receive data is : %s\n", received_data);
	if(!(strcmp(topicName, "D2B"))){
		if(!strcmp(received_data, "request")){
			printf("request arrived \n");			
			ret_from_callback = 1;
		}
		else if(!strcmp(received_data, "fire,true")){
			printf("fire,true arrived\n");
			ret_from_callback = 2;		
		}
	}
	MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
		
	return 1;

}


int main(int argc, char* argv[])
{
	start();
    MQTTClient client_sender;
    MQTTClient client_receiver;
	
    MQTTClient_connectOptions conn_opts_sender = MQTTClient_connectOptions_initializer;
    MQTTClient_connectOptions conn_opts_receiver = MQTTClient_connectOptions_initializer;

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    int person;
    char PAYLOAD[10] = "\0";
    char per[10] = "\0";

    MQTTClient_create(&client_sender, ADDRESS, CLIENTID_SENDER, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts_sender.keepAliveInterval = 20;
    conn_opts_sender.cleansession = 1;

   MQTTClient_create(&client_receiver, ADDRESS_FOR_RECEIVE, CLIENTID_RECEIVER, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts_receiver.keepAliveInterval = 20;
    conn_opts_receiver.cleansession = 1;

    if ((rc = MQTTClient_connect(client_sender, &conn_opts_sender)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
		exit(-1);
    }
	
	// create topic & msg for sender

    MQTTClient_setCallbacks(client_receiver, NULL, connlost, receiver, delivered);
    if ((rc = MQTTClient_connect(client_receiver, &conn_opts_receiver)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        exit(-1);
    }

    MQTTClient_subscribe(client_receiver, TOPIC_D2B, QOS);

    while(1){
	    if(ret_from_callback){
		// room 1

	person = readData();
	sprintf(per, "%d", person);
		strcat(per,",1"); // person + "," + roomNum

		strcpy(PAYLOAD, per);
	    
	    pubmsg.payload = PAYLOAD;
	    pubmsg.payloadlen = strlen(PAYLOAD);
	    pubmsg.qos = QOS;
	    pubmsg.retained = 0;

	    MQTTClient_publishMessage(client_sender, TOPIC_B2D, &pubmsg, &token);
	    printf("Waiting for up to %d seconds for publication of %s\n"
		    "on topic %s for client with ClientID: %s\n",
		    (int)(TIMEOUT/1000), PAYLOAD, TOPIC_B2D, CLIENTID_SENDER);
	    rc = MQTTClient_waitForCompletion(client_sender, token, TIMEOUT);
	    printf("Message with delivery token %d delivered\n", token);
	
		if(ret_from_callback == 2){
			// fire occured
			// control door motor
			// if person 0 -> door close		
			detectFire();
		}


		ret_from_callback = 0;
	    }
    }


	
    MQTTClient_disconnect(client_sender, 10000);
    MQTTClient_destroy(&client_sender);
	end();
    return rc;
}
