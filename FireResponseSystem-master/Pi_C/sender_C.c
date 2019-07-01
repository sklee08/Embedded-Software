
#include "stdio.h"
#include <stdlib.h>
#include "string.h"
#include "MQTTClient.h"
#include "detect_lib.c"
#include <unistd.h>

#define ADDRESS     "tcp://172.20.10.7:1883"
#define CLIENTID    "Client_Sender_C"
#define TOPIC       "C2D"
#define QOS         1
#define TIMEOUT     100L

int main(int argc, char* argv[])
{
    devOpen();
    struct temp_data *temp[3];
    
    int fire = 0;
    int idx = 30;
    int i;

    for(i=0; i<3; i++){
         temp[i] = (struct temp_data *)malloc(sizeof(struct temp_data));
    }
    char* data = (char *)malloc(sizeof(char) * 100);
    char* str = (char *)malloc(sizeof(char) * 50);
    char* tmp_str = (char *)malloc(sizeof(char) * 50);

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        exit(-1);
    }
    

    while(1){
        for(i=0; i<3; i++){
            if(i == 0)
                memset(data, 0, sizeof(data));

            memset(str, 0, sizeof(str));            
            memset(tmp_str, 0, sizeof(tmp_str));
            temp[i]->roomNum = i;
            if(idx == 30){
		        readData(temp[i]);
		        if(i == 2)
		            idx = 0;
            }
            // printf("Room : %d, Temperature : %d.%d, Humidity : %d.%d\n", temp->roomNum, temp->temp1, temp->temp2, temp->hum1, temp->hum2);
            sprintf(str, "%d.%d,%d.%d,", temp[i]->temp1, temp[i]->temp2, temp[i]->hum1, temp[i]->hum2);
            fire = detectFire(i);
            if(fire == 1)
                sprintf(tmp_str, "%d,%c", temp[i]->roomNum, 'O');
            else
                sprintf(tmp_str, "%d,%c", temp[i]->roomNum, 'X');

            strcat(str, tmp_str);

            if(i != 2){
                strcat(str, ",");
            }
            strcat(data, str);
        }

        pubmsg.payload = data;
        pubmsg.payloadlen = strlen(data);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
        printf("Waiting for up to %d seconds for publication of %s\n"
        "on topic %s for client with ClientID: %s\n",
        (int)(TIMEOUT/1000), str, TOPIC, CLIENTID);
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        printf("data : %s\n", data);
        printf("Message with delivery token %d delivered\n", token);
        sleep(1);
        idx++;
    }
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    devClose();
    return rc;
}
